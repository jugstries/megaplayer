// Prova i binding Dart con la libreria vera del core. Serve la variabile
// MEGAPLAYER_CORE con il percorso di libmegaplayer.dylib e STORMLORD_LHA con
// quello di un archivio di prova; senza, il test viene saltato. Suona
// qualche secondo di audio.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/core/ffi_core.dart';
import 'package:megaplayer/core/ffi_library.dart';
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

    core.play(title.ref);
    await Future<void>.delayed(const Duration(seconds: 2));
    final status = core.status();
    expect(status.state, PlaybackState.playing);
    expect(status.position, greaterThan(1.0));
    expect(core.details().player, 'Jeroen Tel');
    expect(core.details().channels, hasLength(4), reason: 'le quattro voci di Paula');
    for (var voice = 0; voice < 4; voice++) {
      core.setChannelMuted(voice, true);
    }
    await Future<void>.delayed(const Duration(milliseconds: 800));
    final muted = core.scope(4096);
    expect(muted.map((v) => v.abs()).reduce((a, b) => a > b ? a : b), lessThan(0.01));
    for (var voice = 0; voice < 4; voice++) {
      core.setChannelMuted(voice, false);
    }
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

  test('playlist, coda e impostazioni con il database vero', () {
    final directory = Directory.systemTemp.createTempSync('megaplayer-ffi');
    addTearDown(() => directory.deleteSync(recursive: true));
    final store = FfiLibraryStore.open(path: '${directory.path}/library.db');
    addTearDown(store.dispose);

    final id = store.createPlaylist('Prova àèì');
    const title = Track(ref: '/Musica/Stormlord.lha/Stormlord/jt.title', name: '/Musica/Stormlord.lha > Stormlord/jt.title');
    store.setEntries(id, [title, title.withSubsong(2), title]);
    final entries = store.entries(id);
    expect(entries.map((e) => e.subsong), [null, 2]);
    expect(entries.first.fileName, 'jt.title');
    expect(store.playlists().single.name, 'Prova àèì');
    expect(store.queueId(), isNot(id));
    store.setSetting('app.skin', 'retro');
    expect(store.setting('app.skin'), 'retro');
    expect(() => store.renamePlaylist(id + 100, 'x'), throwsA(isA<CoreException>()));
  }, skip: library == null ? 'MEGAPLAYER_CORE non impostata' : null);

  test('scansione, ricerca, filtri e cartelle con il database vero', () async {
    final directory = Directory.systemTemp.createTempSync('megaplayer-ffi');
    addTearDown(() => directory.deleteSync(recursive: true));
    final music = '${directory.path}/Musica';
    Directory('$music/Archivio/Toni').createSync(recursive: true);
    File('../tests/data/tono.flac').copySync('$music/Archivio/Toni/tono.flac');
    File('../tests/data/tono.wav').copySync('$music/tono.wav');
    final store = FfiLibraryStore.open(path: '${directory.path}/library.db');
    addTearDown(store.dispose);

    expect(store.root(), isEmpty);
    expect(store.startScan, throwsA(isA<CoreException>()));
    store.setRoot(music);
    store.startScan();
    var status = store.scanStatus();
    for (var i = 0; status.running && i < 1000; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 10));
      status = store.scanStatus();
    }
    expect(status.running, isFalse);
    expect(status.error, isNull);
    expect(status.tracksFound, 2);

    expect(store.search(const LibraryQuery()).total, 2);
    expect(store.folders(const LibraryQuery()).map((f) => (f.name, f.count)), [('Archivio', 1)]);
    expect(store.search(const LibraryQuery(subfolders: false)).items.single.name, 'tono.wav');
    final flac = store.search(const LibraryQuery(formats: {'FLAC'}, folder: 'Archivio')).items.single;
    expect(flac.folder, 'Archivio/Toni');
    expect(flac.platformName, 'File audio');
    expect(store.facets(const LibraryQuery(text: 'tono')).formats.map((f) => f.id), containsAll(['FLAC', 'WAV']));
    expect(store.track(flac.ref)!.durations, hasLength(1));
    expect(store.track('/non/esiste'), isNull);
  }, skip: library == null ? 'MEGAPLAYER_CORE non impostata' : null);

  test('i moduli Roland di 88emu, e con MEGAPLAYER_ROLAND_ROMS le ROM trovate', () {
    final core = FfiPlayerCore.open();
    addTearDown(core.dispose);
    final devices = core.rolandDevices();
    expect(devices.map((d) => d.name), containsAll(['SC-55mkII', 'SC-88Pro']));
    expect(devices.where((d) => d.la), isNotEmpty);
    final roms = Platform.environment['MEGAPLAYER_ROLAND_ROMS'];
    if (roms == null) return;
    core.addRolandRomDirectory(roms);
    expect(core.rolandDevices().where((d) => d.available), isNotEmpty);
  }, skip: library == null ? 'MEGAPLAYER_CORE non impostata' : null);
}
