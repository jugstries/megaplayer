import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'update_manifest.dart';

/// La versione di questa copia dell'app, data alla compilazione dalla build
/// delle release (--dart-define, vedi .github/workflows/release.yml). Le
/// build di sviluppo hanno build 0 e non cercano aggiornamenti.
const appVersion = String.fromEnvironment('MEGAPLAYER_VERSION', defaultValue: 'sviluppo');
const appBuild = int.fromEnvironment('MEGAPLAYER_BUILD');

/// La cartella dei manifesti sul sito; per le prove si cambia con
/// --dart-define=MEGAPLAYER_UPDATES=...
const updatesUrl = String.fromEnvironment(
  'MEGAPLAYER_UPDATES',
  defaultValue: 'https://www.claudiotassitano.it/projects/megaplayer/updates/',
);

/// La chiave pubblica delle release (tool/update_key.dart): l'app installa
/// solo quello che è firmato con la privata, che resta nel Portachiavi del
/// Mac di chi pubblica.
const releasePublicKey = String.fromEnvironment('MEGAPLAYER_UPDATE_KEY', defaultValue: _releaseKey);
const _releaseKey = 'Zgdz8Ir1oM3xIZaTgCWvZMc6Lzu9XXacOAHniQ3B3oY=';

class UpdateException implements Exception {
  const UpdateException(this.message);

  final String message;

  @override
  String toString() => message;
}

/// Cerca sul sito una versione più nuova, la scarica controllandone la firma
/// e lancia l'installer.
class Updater {
  Updater({
    required this.platform,
    required this.currentBuild,
    required this.feed,
    required this.publicKey,
    Directory? downloads,
    HttpClient Function()? httpClient,
    this.launch = launchInstaller,
  })  : downloads = downloads ?? Directory('${Directory.systemTemp.path}${Platform.pathSeparator}megaplayer-update'),
        _httpClient = httpClient ?? HttpClient.new;

  /// L'updater di questa copia dell'app, o null se non si aggiorna da sola:
  /// build di sviluppo, o piattaforma senza installer (per ora c'è solo
  /// quello di Windows).
  static Updater? forThisApp() {
    if (appBuild == 0 || releasePublicKey.isEmpty || !Platform.isWindows) return null;
    return Updater(
      platform: 'windows',
      currentBuild: appBuild,
      feed: Uri.parse(updatesUrl),
      publicKey: releasePublicKey,
    );
  }

  /// "windows" o "macos": il manifesto da leggere.
  final String platform;
  final int currentBuild;

  /// La cartella dei manifesti.
  final Uri feed;

  /// In base64.
  final String publicKey;

  /// Dove finiscono gli installer scaricati.
  final Directory downloads;

  /// Avvia l'installer scaricato; poi l'app si chiude.
  final Future<void> Function(File installer) launch;

  final HttpClient Function() _httpClient;

  static const _timeout = Duration(seconds: 30);

  Uri get manifestUrl => feed.resolve('$platform.json');

  /// La versione più nuova se ce n'è una, firmata da noi; null se questa è
  /// già l'ultima. Lancia [UpdateException].
  Future<UpdateManifest?> check() async {
    final body = await _get(manifestUrl, (response) => response.transform(utf8.decoder).join().timeout(_timeout));
    final UpdateManifest manifest;
    try {
      manifest = UpdateManifest.fromJson(jsonDecode(body) as Map<String, dynamic>);
    } on Object {
      throw const UpdateException('il sito ha risposto con un manifesto illeggibile');
    }
    if (manifest.platform != platform || !await manifest.isSignedBy(publicKey)) {
      throw const UpdateException('l\'aggiornamento sul sito non ha una firma valida');
    }
    return manifest.build > currentBuild ? manifest : null;
  }

  /// Scarica l'installer di [manifest] e controlla che sia proprio quello
  /// firmato. [onProgress] va da 0 a 1. Lancia [UpdateException].
  Future<File> download(UpdateManifest manifest, {void Function(double progress)? onProgress}) async {
    // Solo un nome, accanto al manifesto: niente percorsi.
    if (manifest.file.isEmpty || manifest.file.startsWith('.') || manifest.file.contains(RegExp(r'[/\\:]'))) {
      throw const UpdateException('il manifesto indica un installer non valido');
    }
    await downloads.create(recursive: true);
    final target = File('${downloads.path}${Platform.pathSeparator}${manifest.file}');
    final digest = Sha256Digest();
    var received = 0;
    try {
      await _get(manifestUrl.resolve(manifest.file), (response) async {
        final sink = target.openWrite();
        try {
          await for (final chunk in response.timeout(_timeout)) {
            received += chunk.length;
            if (received > manifest.size) throw const UpdateException('l\'installer è più grande di quello firmato');
            digest.add(chunk);
            sink.add(chunk);
            onProgress?.call(received / manifest.size);
          }
        } finally {
          await sink.close();
        }
      });
      if (received != manifest.size || await digest.finish() != manifest.sha256) {
        throw const UpdateException('l\'installer scaricato non è quello firmato');
      }
    } on UpdateException {
      // Niente installer a metà o diversi da quello firmato sul disco.
      if (await target.exists()) await target.delete();
      rethrow;
    }
    return target;
  }

  /// Toglie gli installer delle volte passate (il registro dell'ultima
  /// installazione resta, per capire cosa è andato storto).
  Future<void> cleanUp() async {
    try {
      if (!await downloads.exists()) return;
      await for (final entry in downloads.list()) {
        if (entry is File && entry.path.toLowerCase().endsWith('.exe')) await entry.delete();
      }
    } on FileSystemException {
      // Un installer ancora aperto: sarà per la prossima volta.
    }
  }

  Future<T> _get<T>(Uri url, Future<T> Function(HttpClientResponse response) read) async {
    final client = _httpClient()..connectionTimeout = const Duration(seconds: 15);
    try {
      final request = await client.getUrl(url);
      final response = await request.close().timeout(_timeout);
      if (response.statusCode != HttpStatus.ok) {
        throw UpdateException('il sito ha risposto ${response.statusCode}');
      }
      return await read(response);
    } on SocketException {
      throw const UpdateException('il sito non risponde: sei in rete?');
    } on TimeoutException {
      throw const UpdateException('il sito non risponde');
    } on HandshakeException {
      throw const UpdateException('la connessione sicura con il sito non è riuscita');
    } on HttpException catch (e) {
      throw UpdateException('errore di rete: ${e.message}');
    } on FileSystemException catch (e) {
      throw UpdateException('impossibile salvare l\'installer: ${e.message}');
    } finally {
      client.close(force: true);
    }
  }
}

/// Lancia l'installer di Windows (Inno Setup) senza le sue domande: mostra
/// solo l'avanzamento, e alla fine riapre l'app. Chi chiama chiude l'app
/// subito dopo, perché l'installer possa sostituirne i file.
Future<void> launchInstaller(File installer) async {
  final log = '${installer.parent.path}${Platform.pathSeparator}installazione.log';
  try {
    await Process.start(
      installer.path,
      ['/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CLOSEAPPLICATIONS', '/LOG=$log'],
      mode: ProcessStartMode.detached,
    );
  } on ProcessException catch (e) {
    throw UpdateException('impossibile avviare l\'installer: ${e.message}');
  }
}
