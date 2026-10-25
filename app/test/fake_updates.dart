import 'dart:async';
import 'dart:io';

import 'package:megaplayer/update/update_manifest.dart';
import 'package:megaplayer/update/updater.dart';

/// Una versione nuova, come la pubblicherebbe tool/sign_update.dart.
const newRelease = UpdateManifest(
  platform: 'windows',
  version: '0.2.0',
  build: 2,
  date: '2026-10-25',
  notes: '• Solo e mute dei canali, in tutte e due le skin.\n'
      '• megaplayer si aggiorna da solo.\n'
      '• La versione per Windows.',
  file: 'megaplayer-0.2.0-windows-setup.exe',
  size: 25480000,
  sha256: '00',
);

/// Un updater senza rete: "trova" [found], e scaricando si ferma al 42%
/// finché [finishDownload] non lo lascia finire.
class FakeUpdater extends Updater {
  factory FakeUpdater({UpdateManifest? found}) => FakeUpdater._([], found: found);

  FakeUpdater._(this.launched, {this.found})
      : super(
          platform: 'windows',
          currentBuild: 1,
          feed: Uri.parse('https://example.invalid/updates/'),
          publicKey: '',
          launch: (installer) async => launched.add(installer.path),
        );

  UpdateManifest? found;

  /// Gli installer lanciati.
  final List<String> launched;

  final _download = Completer<void>();

  void finishDownload() => _download.complete();

  @override
  Future<UpdateManifest?> check() async => found;

  @override
  Future<File> download(UpdateManifest manifest, {void Function(double progress)? onProgress}) async {
    onProgress?.call(0.42);
    await _download.future;
    return File('installer/${manifest.file}');
  }

  @override
  Future<void> cleanUp() async {}
}
