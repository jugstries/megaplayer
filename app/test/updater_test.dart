// Gli aggiornamenti contro un sito finto in locale: firma del manifesto,
// confronto delle build, installer scaricato e controllato, e gli stati che
// vedono le skin.

import 'dart:convert';
import 'dart:io';

import 'package:cryptography/cryptography.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/update/update_manifest.dart';
import 'package:megaplayer/update/updater.dart';
import 'package:megaplayer/update/updates.dart';

import 'fake_updates.dart';

void main() {
  final seed = List<int>.generate(32, (i) => i * 7 + 1);
  final otherSeed = List<int>.generate(32, (i) => 255 - i);
  late String publicKey;
  final installer = utf8.encode('MZ finto installer di megaplayer ' * 2000);
  late String installerHash;

  late HttpServer server;
  late Directory downloads;
  // Quello che il sito finto risponde, per percorso.
  final pages = <String, List<int>>{};

  setUpAll(() async {
    final keyPair = await Ed25519().newKeyPairFromSeed(seed);
    publicKey = base64.encode((await keyPair.extractPublicKey()).bytes);
    final digest = Sha256Digest()..add(installer);
    installerHash = await digest.finish();
  });

  setUp(() async {
    pages.clear();
    server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    server.listen((request) async {
      final body = pages[request.uri.path];
      request.response.statusCode = body == null ? HttpStatus.notFound : HttpStatus.ok;
      if (body != null) request.response.add(body);
      await request.response.close();
    });
    downloads = await Directory.systemTemp.createTemp('megaplayer-update-test');
  });

  tearDown(() async {
    await server.close(force: true);
    await downloads.delete(recursive: true);
  });

  UpdateManifest release({int build = 2, int? size, String? sha256, String file = 'megaplayer-0.2.0-setup.exe'}) =>
      UpdateManifest(
        platform: 'windows',
        version: '0.2.0',
        build: build,
        date: '2026-10-25',
        notes: 'Novità\nsu due righe',
        file: file,
        size: size ?? installer.length,
        sha256: sha256 ?? installerHash,
      );

  Future<void> publish(UpdateManifest manifest, {List<int>? key}) async {
    final signed = await manifest.signedWith(key ?? seed);
    pages['/updates/windows.json'] = utf8.encode(jsonEncode(signed.toJson()));
    pages['/updates/${manifest.file}'] = installer;
  }

  Updater updater({int currentBuild = 1, List<String>? launched}) => Updater(
        platform: 'windows',
        currentBuild: currentBuild,
        feed: Uri.parse('http://127.0.0.1:${server.port}/updates/'),
        publicKey: publicKey,
        downloads: downloads,
        launch: (file) async => launched?.add(file.path),
      );

  group('manifesto', () {
    test('la firma copre tutti i campi', () async {
      final signed = await release().signedWith(seed);
      expect(await signed.isSignedBy(publicKey), isTrue);

      final json = signed.toJson();
      for (final change in <String, Object>{'build': 3, 'notes': 'altro', 'sha256': 'ff', 'file': 'x.exe'}.entries) {
        final tampered = UpdateManifest.fromJson({...json, change.key: change.value});
        expect(await tampered.isSignedBy(publicKey), isFalse, reason: change.key);
      }
      final other = await release().signedWith(otherSeed);
      expect(await other.isSignedBy(publicKey), isFalse);
    });

    test('un manifesto incompleto non si legge', () {
      final json = release().toJson()..remove('sha256');
      expect(() => UpdateManifest.fromJson(json), throwsFormatException);
    });
  });

  group('updater', () {
    test('trova solo build più nuove', () async {
      await publish(release(build: 5));
      expect((await updater(currentBuild: 4).check())!.version, '0.2.0');
      expect(await updater(currentBuild: 5).check(), isNull);
      expect(await updater(currentBuild: 6).check(), isNull);
    });

    test('rifiuta un manifesto firmato da altri, o illeggibile', () async {
      await publish(release(), key: otherSeed);
      await expectLater(updater().check(), throwsA(isA<UpdateException>()));
      pages['/updates/windows.json'] = utf8.encode('<html>niente</html>');
      await expectLater(updater().check(), throwsA(isA<UpdateException>()));
    });

    test('senza manifesto sul sito, o senza sito, è un errore', () async {
      await expectLater(updater().check(), throwsA(isA<UpdateException>()));
      final port = server.port;
      await server.close(force: true);
      final offline = Updater(
        platform: 'windows',
        currentBuild: 1,
        feed: Uri.parse('http://127.0.0.1:$port/updates/'),
        publicKey: publicKey,
      );
      await expectLater(offline.check(), throwsA(isA<UpdateException>()));
    });

    test('scarica l\'installer firmato e dice quanto manca', () async {
      final manifest = release();
      await publish(manifest);
      final progress = <double>[];
      final file = await updater().download(manifest, onProgress: progress.add);
      expect(await file.readAsBytes(), installer);
      expect(file.parent.path, downloads.path);
      expect(progress.last, 1.0);
    });

    test('butta un installer diverso da quello firmato', () async {
      for (final manifest in [release(sha256: 'ab' * 32), release(size: installer.length - 1)]) {
        await publish(manifest);
        await expectLater(updater().download(manifest), throwsA(isA<UpdateException>()));
        expect(File('${downloads.path}/${manifest.file}').existsSync(), isFalse);
      }
    });

    test('accetta solo nomi di file, non percorsi', () async {
      for (final file in ['../evil.exe', 'a/b.exe', r'C:\x.exe', '.hidden', '']) {
        await expectLater(updater().download(release(file: file)), throwsA(isA<UpdateException>()), reason: file);
      }
    });

    test('pulisce gli installer vecchi ma tiene il registro', () async {
      File('${downloads.path}/vecchio-setup.exe').writeAsStringSync('x');
      File('${downloads.path}/installazione.log').writeAsStringSync('ok');
      await updater().cleanUp();
      expect(downloads.listSync().map((e) => e.uri.pathSegments.last), ['installazione.log']);
    });
  });

  group('stati per le skin', () {
    test('trovata all\'avvio, annunciata una volta, poi installata', () async {
      await publish(release());
      final launched = <String>[];
      var quits = 0;
      final updates = Updates(updater(launched: launched), quit: () async => quits++, currentVersion: '0.1.0');

      await updates.start();
      expect(updates.stage, UpdateStage.available);
      expect(updates.shouldAnnounce, isTrue);
      updates.markAnnounced();
      expect(updates.shouldAnnounce, isFalse);

      final stages = <UpdateStage>[];
      updates.addListener(() => stages.add(updates.stage));
      await updates.install();
      expect(stages.first, UpdateStage.downloading);
      expect(stages.last, UpdateStage.installing);
      expect(launched, hasLength(1));
      expect(quits, 1);
    });

    test('senza rete all\'avvio non dice niente; cercando a mano sì', () async {
      final updates = Updates(updater(), quit: () async {}, currentVersion: '0.1.0');
      await updates.check();
      expect(updates.stage, UpdateStage.idle);
      expect(updates.problem, isNull);
      await updates.check(manual: true);
      expect(updates.stage, UpdateStage.failed);
      expect(updates.problem, isNotNull);
    });

    test('cercando a mano l\'ultima versione lo dice, e niente annuncio', () async {
      await publish(release(build: 1));
      final updates = Updates(updater(), quit: () async {}, currentVersion: '0.1.0');
      await updates.check(manual: true);
      expect(updates.stage, UpdateStage.upToDate);
      expect(updates.shouldAnnounce, isFalse);
    });

    test('un installer rovinato non chiude l\'app e si può riprovare', () async {
      final manifest = release();
      await publish(manifest);
      pages['/updates/${manifest.file}'] = utf8.encode('rovinato');
      var quits = 0;
      final updates = Updates(updater(), quit: () async => quits++, currentVersion: '0.1.0');
      await updates.check();
      await updates.install();
      expect(updates.stage, UpdateStage.failed);
      expect(updates.offer, isNotNull);
      expect(quits, 0);
    });

    test('il finto delle skin si ferma a metà scaricamento', () async {
      final fake = FakeUpdater(found: newRelease);
      final updates = Updates(fake, quit: () async {}, currentVersion: '0.1.0');
      await updates.check();
      final installing = updates.install();
      await Future<void>.delayed(Duration.zero);
      expect(updates.stage, UpdateStage.downloading);
      expect(updates.progress, closeTo(0.42, 0.001));
      fake.finishDownload();
      await installing;
      expect(fake.launched, hasLength(1));
    });
  });
}
