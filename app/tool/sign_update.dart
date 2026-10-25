// Firma un installer con la chiave del Portachiavi (tool/update_key.dart) e
// scrive in --out il manifesto che l'app legge, <piattaforma>.json, con una
// copia dell'installer accanto.
//
// Uso (in app/):
//   dart run tool/sign_update.dart --installer FILE --platform windows \
//     --version 0.2.0 --build 2 --notes NOVITÀ.txt --out DIR

import 'dart:convert';
import 'dart:io';

import 'package:megaplayer/update/update_manifest.dart';
import 'package:megaplayer/update/updater.dart';

import 'release_key.dart';

Future<void> main(List<String> arguments) async {
  final options = <String, String>{};
  for (var i = 0; i + 1 < arguments.length; i += 2) {
    options[arguments[i].replaceFirst('--', '')] = arguments[i + 1];
  }
  String option(String name) => options[name] ?? (throw ArgumentError('manca --$name'));

  final installer = File(option('installer'));
  final out = Directory(option('out'));
  final seed = await readReleaseSeed();
  if (seed == null) throw StateError('nessuna chiave nel Portachiavi: dart run tool/update_key.dart');
  if (await publicKeyOf(seed) != releasePublicKey) {
    throw StateError('la chiave del Portachiavi non è quella scritta in lib/update/updater.dart');
  }

  final digest = Sha256Digest();
  await installer.openRead().forEach(digest.add);
  final now = DateTime.now();
  final date = '${now.year}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';
  final manifest = await UpdateManifest(
    platform: option('platform'),
    version: option('version'),
    build: int.parse(option('build')),
    date: date,
    notes: (await File(option('notes')).readAsString()).trim(),
    file: installer.uri.pathSegments.last,
    size: await installer.length(),
    sha256: await digest.finish(),
  ).signedWith(seed);
  if (!await manifest.isSignedBy(releasePublicKey)) throw StateError('la firma appena fatta non si verifica');

  await out.create(recursive: true);
  await installer.copy('${out.path}/${manifest.file}');
  final json = File('${out.path}/${manifest.platform}.json');
  await json.writeAsString('${const JsonEncoder.withIndent('  ').convert(manifest.toJson())}\n');
  stdout.writeln('${json.path}: ${manifest.version} (build ${manifest.build}), ${manifest.size} byte, firmato');
}
