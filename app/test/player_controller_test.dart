import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/core/models.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fake_library.dart';

void main() {
  late FakeCore core;
  late PlayerController controller;

  setUp(() {
    core = FakeCore();
    controller = PlayerController(core);
  });

  test('aggiungere un archivio mette in coda i brani e avvia il primo', () {
    controller.addPaths([FakeCore.archive]);
    expect(controller.tracks.map((t) => t.fileName), [
      'cust.title', 'jt.ingame', 'jt.title', 'mon.ingame', 'mon.title', 'jt.ingame',
    ]);
    expect(controller.tracks[3].location, 'Stormlord.lha · Stormlord · MON_Old_Version');
    expect(controller.currentIndex, 0);
    expect(core.calls, ['play cust.title']);
  });

  test('un file che non si apre diventa un messaggio, non un crash', () {
    controller.addPaths(['/non/esiste.mod']);
    expect(controller.tracks, isEmpty);
    expect(controller.error, contains('esiste.mod'));
  });

  test('a fine brano passa al successivo, a fine coda si ferma', () {
    controller.addPaths([FakeCore.archive]);
    core.state = PlaybackState.ended;
    controller.refresh();
    expect(controller.currentIndex, 1);

    // L'ultima voce ha cinque sottobrani: finito l'ultimo, ci si ferma.
    controller.playIndex(5);
    core.subsong = 4;
    core.state = PlaybackState.ended;
    controller.refresh();
    expect(controller.currentIndex, 5);
    expect(core.playing, isNull);
  });

  test('una voce col file intero suona i sottobrani in fila, una col sottobrano solo quello', () {
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(1); // jt.ingame, cinque sottobrani
    core.calls.clear();
    core.state = PlaybackState.ended;
    controller.refresh();
    expect(controller.currentIndex, 1);
    expect(core.calls, ['subsong 1']);

    // Con il sottobrano scelto parte quello, e alla fine si passa avanti.
    expect(controller.setTrackSubsong(3, 2), isTrue);
    core.calls.clear();
    controller.playIndex(3);
    expect(core.calls, ['play mon.ingame', 'subsong 2']);
    core.state = PlaybackState.ended;
    controller.refresh();
    expect(controller.currentIndex, 4);
  });

  test('"precedente" riparte dall\'inizio dopo i primi secondi', () {
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(2);
    core.position = 40;
    controller.refresh();
    controller.previous();
    expect(controller.currentIndex, 2);
    expect(core.calls.last, 'seek 0');

    controller.previous();
    expect(controller.currentIndex, 1);
  });

  test('i sottobrani restano nei limiti', () {
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(1); // jt.ingame, 5 sottobrani
    controller.selectSubsong(4);
    controller.selectSubsong(5);
    controller.selectSubsong(-1);
    expect(core.calls.where((c) => c.startsWith('subsong')), ['subsong 4']);
  });

  test('mute e solo dei canali arrivano al core, e il brano dopo riparte con tutti', () {
    controller.addPaths([FakeCore.archive]);
    expect(controller.channels, hasLength(4));
    core.calls.clear();

    controller.toggleChannelMute(1);
    expect(controller.channelAudible(1), isFalse);
    expect(core.calls, ['mute 1']);

    // In solo si sentono solo quelli in solo, anche se zittiti.
    core.calls.clear();
    controller.toggleChannelSolo(1);
    controller.toggleChannelSolo(3);
    expect([for (var i = 0; i < 4; i++) controller.channelAudible(i)], [false, true, false, true]);
    expect(core.calls, ['mute 0', 'unmute 1', 'mute 2', 'mute 3', 'unmute 3']);

    core.calls.clear();
    controller.resetChannels();
    expect(core.calls, ['unmute 0', 'unmute 2']);
    expect(controller.mutedChannels, isEmpty);

    controller.toggleChannelMute(0);
    controller.toggleChannelMute(9); // non c'è
    controller.selectSubsong(0);
    expect(controller.channelAudible(0), isFalse, reason: 'il mute resta cambiando sottobrano');
    controller.next();
    expect(controller.mutedChannels, isEmpty);
    expect(controller.channelAudible(0), isTrue);
  });

  test("in pausa l'oscilloscopio si appiattisce piano piano, poi riparte", () {
    controller.addPaths([FakeCore.archive]);
    controller.refresh();
    double peak() => controller.scope.fold(0.0, (top, v) => math.max(top, v.abs()));
    final playing = peak();
    expect(playing, greaterThan(0.3));

    core.state = PlaybackState.paused;
    controller.refresh();
    expect(peak(), lessThan(playing)); // scende, ma non di colpo
    expect(peak(), greaterThan(playing * 0.5));
    for (var i = 0; i < 20; i++) {
      controller.refresh();
    }
    expect(peak(), 0);
    final flat = controller.scope;
    controller.refresh();
    expect(identical(controller.scope, flat), isTrue); // piatto: niente da ridisegnare

    core.state = PlaybackState.playing;
    controller.refresh();
    expect(peak(), playing);
  });

  test('la manopola MegaSound arriva al core, il confronto A/B manda 0', () {
    controller.setMegaSound(0.7);
    controller.toggleMegaSoundCompare();
    expect(controller.megaSoundBypassed, isTrue);
    expect(controller.megaSound, 0.7);
    controller.toggleMegaSoundCompare();
    // Girare la manopola durante il confronto torna a far sentire l'effetto.
    controller.toggleMegaSoundCompare();
    controller.setMegaSound(1.4);
    expect(controller.megaSoundBypassed, isFalse);
    expect(core.calls.where((c) => c.startsWith('megasound')),
        ['megasound 70', 'megasound 0', 'megasound 70', 'megasound 0', 'megasound 100']);
  });

  group('con il database', () {
    late FakeLibraryStore store;

    PlayerController reopen() {
      final next = PlayerController(FakeCore(), store: store)..restore();
      return next;
    }

    setUp(() {
      store = FakeLibraryStore();
      controller = PlayerController(core, store: store)..restore();
    });

    test('coda, brano corrente, skin, volume e manopola restano al riavvio', () {
      controller.addPaths([FakeCore.archive]);
      controller.playIndex(2);
      controller.setSkin(Skin.retro);
      controller.setVolume(0.5);
      controller.setMegaSound(0.7);

      final next = reopen();
      expect(next.tracks.map((t) => t.ref), controller.tracks.map((t) => t.ref));
      expect(next.currentIndex, 2);
      expect(next.status.state, PlaybackState.idle, reason: 'riprende la coda senza farla partire');
      expect(next.skin, Skin.retro);
      expect(next.volume, 0.5);
      expect(next.megaSound, 0.7);
    });

    test('la stessa voce non entra due volte, con un altro sottobrano sì', () {
      controller.addPaths([FakeCore.archive]);
      controller.addPaths([FakeCore.archive]);
      expect(controller.tracks, hasLength(6));
      expect(controller.setTrackSubsong(1, 1), isTrue);
      controller.addPaths([FakeCore.archive]);
      expect(controller.tracks, hasLength(7), reason: 'jt.ingame intero torna, accanto al sottobrano 2');
      expect(controller.setTrackSubsong(6, 1), isFalse, reason: 'jt.ingame sottobrano 2 c\'è già');
    });

    test('togliere e spostare voci tiene il brano corrente', () {
      controller.addPaths([FakeCore.archive]);
      controller.playIndex(2);
      controller.removeTrack(0);
      expect(controller.currentIndex, 1);
      controller.moveTrack(1, 4);
      expect(controller.currentIndex, 4);
      expect(controller.currentTrack!.fileName, 'jt.title');
      controller.moveTrack(0, 4);
      expect(controller.currentIndex, 3);
      controller.removeTrack(3);
      expect(controller.currentIndex, isNull);
      expect(core.playing, isNull);
      expect(reopen().tracks, hasLength(4));
    });

    test('playlist: crea, rinomina, aggiungi, sposta, togli, carica, cancella', () {
      controller.addPaths([FakeCore.archive]);
      final id = controller.saveQueueAsPlaylist('Stormlord')!;
      expect(controller.playlists.single.name, 'Stormlord');
      expect(controller.playlists.single.count, 6);

      final mine = controller.createPlaylist('Preferiti')!;
      controller.renamePlaylist(mine, 'I miei preferiti');
      final title = controller.tracks[2];
      expect(controller.addToPlaylist(mine, [title, title.withSubsong(0), title]), 2);
      expect(controller.addToPlaylist(mine, [title]), 0);
      controller.movePlaylistTrack(mine, 1, 0);
      expect(controller.playlistTracks(mine).first.subsong, 0);
      expect(controller.setPlaylistSubsong(mine, 0, null), isFalse, reason: 'il file intero c\'è già');
      controller.removeFromPlaylist(mine, 1);
      expect(controller.playlistTracks(mine).single.subsong, 0);

      controller.loadPlaylist(mine);
      expect(controller.tracks.single.subsong, 0);
      expect(core.calls.last, 'subsong 0');
      expect(reopen().tracks.single.subsong, 0);

      controller.deletePlaylist(id);
      expect(controller.playlists.map((p) => p.name), ['I miei preferiti']);
    });

    test('i file M3U aperti o trascinati diventano playlist, gli altri vanno in coda', () {
      store.m3uFiles['/Liste/Sera.m3u8'] = [
        const Track(ref: '${FakeCore.archive}/Stormlord/jt.title', name: 'jt.title', subsong: 0),
      ];
      final result = controller.addPaths(['/Liste/Sera.m3u8', FakeCore.archive, '/Liste/Vuota.M3U']);
      expect(result.playlists.single.name, 'Sera');
      expect(result.playlists.single.count, 1);
      expect([result.added, result.requested], [6, 6]);
      expect(controller.tracks, hasLength(6), reason: "l'M3U non va in coda");
      expect(controller.playlists.map((p) => p.name), ['Sera']);
      expect(controller.playlistTracks(result.playlists.single.id).single.subsong, 0);
      expect(controller.error, 'nessun brano di Vuota.M3U si trova');
    });

    test('dalla libreria: suona subito, in coda, sottobrani', () {
      controller.addPaths([FakeCore.archive]);
      controller.playIndex(1);
      final nebula = libraryItem('Amiga', 'nebula.mod', subsongs: 3, durations: [60, null, 30]);
      store.items.add(nebula);

      // Suona subito: dopo la voce corrente; se c'è già, da dov'è.
      controller.playNow(nebula.toTrack(subsong: 2));
      expect(controller.currentIndex, 2);
      expect(controller.currentTrack!.subsong, 2);
      expect(core.calls.last, 'play nebula.mod');
      controller.playNow(controller.tracks[0]);
      expect(controller.currentIndex, 0);
      expect(controller.tracks, hasLength(7));

      expect(controller.enqueue([nebula.toTrack(), nebula.toTrack(subsong: 2)]), 1);
      expect(controller.tracks.last.subsong, isNull);
      expect(reopen().tracks, hasLength(8));

      expect(controller.subsongsOf(nebula.toTrack()), [60, null, 30]);
      // Fuori dalla libreria i sottobrani si sanno solo da quello che suona.
      controller.playIndex(4); // jt.ingame, cinque sottobrani
      expect(controller.subsongsOf(controller.tracks[4]), hasLength(5));
      expect(controller.subsongsOf(controller.tracks[5]), isNull);
      core.subsong = 3;
      controller.refresh();
      expect(controller.playingEntry!.subsong, 3);
    });

    test('caricare una playlist da una voce', () {
      controller.addPaths([FakeCore.archive]);
      final id = controller.saveQueueAsPlaylist('Stormlord')!;
      controller.clearQueue();
      controller.loadPlaylist(id, start: 3);
      expect(controller.tracks, hasLength(6));
      expect(controller.currentIndex, 3);
    });

    test('la libreria segue la scansione dal timer del player', () {
      store.rootPath = '/Musica';
      final next = reopen();
      expect(next.library.scanning, isTrue);
      store.finishScan([libraryItem('', 'giro.sid')]);
      next.refresh();
      expect(next.library.scanning, isFalse);
      expect(next.library.items.single.name, 'giro.sid');
    });

    test('senza database tutto funziona e niente resta', () {
      final alone = PlayerController(FakeCore())..restore();
      alone.addPaths([FakeCore.archive]);
      expect(alone.tracks, hasLength(6));
      expect(alone.createPlaylist('x'), isNull);
      expect(alone.playlists, isEmpty);
    });
  });

  test('cartella delle ROM e Sound Canvas: al core subito, e di nuovo al riavvio', () {
    final store = FakeLibraryStore();
    final first = PlayerController(core, store: store)..restore();
    expect(first.soundCanvases, isEmpty);
    first.setRomFolder('/Roms/Roland');
    first.setRolandDevice('SC-88Pro');
    expect(first.soundCanvases.map((d) => d.name), ['SC-55mkII', 'SC-88Pro']);
    expect(core.rolandDevice, 'SC-88Pro');

    final restarted = FakeCore();
    final second = PlayerController(restarted, store: store)..restore();
    expect(restarted.romDirectories, ['/Roms/Roland']);
    expect(restarted.rolandDevice, 'SC-88Pro');
    expect(second.romFolder, '/Roms/Roland');
    expect(second.soundCanvases, hasLength(2));
  });

  test('il SoundFont dei MIDI senza ROM: scelto, ricordato e dato al core al riavvio', () {
    final store = FakeLibraryStore();
    final first = PlayerController(core, store: store)..restore();
    expect(first.soundFontSupported, isTrue);
    expect(core.currentSoundFont, '/App/soundfonts/GeneralUser-GS.sf2');
    first.setSoundFontChoice(SoundFontChoice.fm);
    expect(core.currentSoundFont, '');

    final restarted = FakeCore();
    PlayerController(restarted, store: store).restore();
    expect(restarted.currentSoundFont, '');
    store.settings['midi.soundfont'] = 'custom';
    store.settings['midi.soundfont.path'] = '/SoundFont/Arachno.sf2';
    final again = FakeCore();
    PlayerController(again, store: store).restore();
    expect(again.currentSoundFont, '/SoundFont/Arachno.sf2');
  });

  test('esportazione: il brano com\'è adesso, poi la riproduzione riprende da dov\'era', () {
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(2);
    core.position = 72;
    controller
      ..toggleChannelMute(1)
      ..setMegaSound(0.6)
      ..refresh();
    controller.startExport('/Export/jt.title.mp3');
    expect(core.exports, ['jt.title 0 /Export/jt.title.mp3 60 [1] "jt.title"']);
    expect(controller.exporting, isTrue);

    core.calls.clear();
    core.exportState = const ExportStatus(seconds: 184, total: 184);
    controller.refresh();
    expect(controller.exporting, isFalse);
    expect(controller.exportJob!.done, isTrue);
    expect(core.calls, containsAllInOrder(['play jt.title', 'mute 1', 'seek 72']));
    expect(controller.mutedChannels, {1});

    // Senza MegaSound né canali, se non si vogliono.
    controller.dismissExport();
    controller.startExport('/Export/jt.title.wav', withMegaSound: false, withChannels: false);
    expect(core.exports.last, 'jt.title 0 /Export/jt.title.wav 0 [] "jt.title"');
  });
}
