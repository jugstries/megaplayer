// Prova i binding Dart con la libreria vera del core. Serve la variabile
// MEGAPLAYER_CORE con il percorso di libmegaplayer.dylib e STORMLORD_LHA con
// quello di un archivio di prova; senza, il test viene saltato. Suona
// qualche secondo di audio.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/core/ffi_core.dart';
import 'package:megaplayer/core/models.dart';

void main() {
  final library = Platform.environment['MEGAPLAYER_CORE'];
  final archive = Platform.environment['STORMLORD_LHA'];
  final skip = library == null || archive == null ? 'MEGAPLAYER_CORE e STORMLORD_LHA non impostate' : null;

  test('archivio, riproduzione, stato e oscilloscopio con il core vero', () async {
    final core = FfiPlayerCore.open();
    addTearDown(core.dispose);

    expect(core.backends().map((b) => b.name), containsAll(['openmpt', 'uade']));

    final tracks = core.collect(archive!);
    expect(tracks, hasLength(6));
    final title = tracks.firstWhere((t) => t.name.endsWith('Stormlord/jt.title'));

    core.play(title.path);
    await Future<void>.delayed(const Duration(seconds: 2));
    final status = core.status();
    expect(status.state, PlaybackState.playing);
    expect(status.position, greaterThan(1.0));
    expect(core.details().player, 'Jeroen Tel');
    expect(core.scope(512), hasLength(1024));

    core.seek(30);
    await Future<void>.delayed(const Duration(milliseconds: 600));
    expect(core.status().position, greaterThan(29.0));

    // MegaSound al massimo: il limiter tiene i picchi sotto −0.5 dBFS.
    core.setMegaSound(1);
    await Future<void>.delayed(const Duration(seconds: 1));
    final samples = core.scope(4096);
    expect(samples, isNotEmpty);
    expect(samples.map((v) => v.abs()).reduce((a, b) => a > b ? a : b), lessThanOrEqualTo(0.945));
    core.setMegaSound(0);

    expect(() => core.play('/non/esiste.mod'), throwsA(isA<CoreException>()));
  }, skip: skip);
}
