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
  /// build di sviluppo, o piattaforma senza release (Linux).
  static Updater? forThisApp() {
    if (appBuild == 0 || releasePublicKey.isEmpty) return null;
    final (platform, launch) = Platform.isWindows
        ? ('windows', launchInstaller)
        : Platform.isMacOS
            ? ('macos', replaceAppBundle)
            : (null, null);
    if (platform == null || launch == null) return null;
    return Updater(
      platform: platform,
      currentBuild: appBuild,
      feed: Uri.parse(updatesUrl),
      publicKey: releasePublicKey,
      launch: launch,
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

  /// Avvia l'installazione di quello che è stato scaricato (l'installer su
  /// Windows, lo zip dell'app su macOS); poi l'app si chiude.
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

  /// Toglie quello che è stato scaricato le volte passate (il registro
  /// dell'ultima installazione resta, per capire cosa è andato storto).
  Future<void> cleanUp() async {
    try {
      if (!await downloads.exists()) return;
      await for (final entry in downloads.list()) {
        if (!entry.path.endsWith('.log')) await entry.delete(recursive: true);
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

/// Su macOS: estrae l'app dallo zip scaricato e lascia il cambio a uno
/// script, che aspetta la chiusura di questa copia, mette la nuova al suo
/// posto (rimettendo la vecchia se qualcosa va storto) e la riapre. Chi
/// chiama chiude l'app subito dopo.
///
/// [target] è l'app da sostituire (di solito questa), [waitFor] il processo
/// da aspettare (questo); servono ai test, che non la riaprono
/// ([relaunch]).
Future<void> replaceAppBundle(File archive, {Directory? target, int? waitFor, bool relaunch = true}) async {
  final app = target ?? File(Platform.resolvedExecutable).parent.parent.parent;
  // Aperta senza spostarla da Download, macOS la fa girare da una copia
  // temporanea in sola lettura.
  if (app.path.contains('/AppTranslocation/')) {
    throw const UpdateException('megaplayer gira da una cartella temporanea di macOS: '
        'spostalo in Applicazioni, riaprilo e riprova');
  }
  final probe = File('${app.parent.path}/.megaplayer-aggiornamento');
  try {
    await probe.writeAsString('');
    await probe.delete();
  } on FileSystemException {
    throw UpdateException('non posso scrivere in ${app.parent.path}: scarica la versione nuova dal sito');
  }

  final staging = Directory('${archive.parent.path}/nuova');
  if (await staging.exists()) await staging.delete(recursive: true);
  final unzip = await Process.run('/usr/bin/ditto', ['-x', '-k', archive.path, staging.path]);
  final fresh = unzip.exitCode != 0
      ? null
      : (await staging.list().toList()).where((entry) => entry is Directory && entry.path.endsWith('.app')).firstOrNull;
  if (fresh == null) throw const UpdateException('lo zip scaricato non contiene l\'app');

  final script = File('${archive.parent.path}/aggiorna.sh');
  await script.writeAsString(_swapScript);
  await Process.start(
    '/bin/sh',
    [script.path, '${waitFor ?? pid}', fresh.path, app.path, '${archive.parent.path}/installazione.log', '$relaunch'],
    mode: ProcessStartMode.detached,
  );
}

const _swapScript = r"""
# Aspetta che megaplayer si chiuda, mette la versione nuova al posto della
# vecchia, che torna dov'era se qualcosa va storto, e la riapre.
# Uso: aggiorna.sh PID NUOVA.app VECCHIA.app REGISTRO RIAPRI
pid=$1 new=$2 target=$3 relaunch=$5
exec >>"$4" 2>&1
echo "$(date): aggiorno $target"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
rm -rf "$target.nuova" "$target.vecchia"
if /usr/bin/ditto "$new" "$target.nuova" && mv "$target" "$target.vecchia" && mv "$target.nuova" "$target"; then
  rm -rf "$target.vecchia"
  echo "fatto"
else
  [ -d "$target" ] || mv "$target.vecchia" "$target"
  rm -rf "$target.nuova"
  echo "non riuscito: resta la versione di prima"
fi
[ "$relaunch" = true ] && open "$target"
exit 0
""";
