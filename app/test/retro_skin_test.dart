// Libreria, coda, playlist e aggiornamenti della skin retrò, usati come un
// utente: clic sulle linguette, menu a comparsa e finestre di dialogo, con
// il core, il database e il sito finti.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/core/models.dart';
import 'package:megaplayer/player/player_controller.dart';
import 'package:megaplayer/update/updates.dart';

import 'fake_core.dart';
import 'fake_library.dart';
import 'fake_updates.dart';
import 'fonts.dart';

void main() {
  late FakeCore core;
  late FakeLibraryStore store;
  late PlayerController controller;

  setUpAll(loadAppFonts);

  setUp(() {
    core = FakeCore();
    store = FakeLibraryStore()..rootPath = '/Musica';
    controller = PlayerController(core, store: store)
      ..restore()
      ..setSkin(Skin.retro);
    store.finishScan([
      libraryItem('', 'giro.sid', platform: 'c64', format: 'PSID', subsongs: 3, durations: [60, 90, null]),
      libraryItem('Amiga/Jesper Kyd', 'nebula.mod', title: 'Nebula'),
      libraryItem('Amiga/Jesper Kyd', 'heroes.med', format: 'MED'),
    ]);
    controller.refresh();
  });

  Future<void> open(WidgetTester tester, {String tab = 'LIBRERIA'}) async {
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.text(tab));
    await tester.pump();
  }

  /// Le voci dei menu agiscono al frame dopo la chiusura.
  Future<void> choose(WidgetTester tester, Finder item) async {
    await tester.tap(item);
    await tester.pump();
    await tester.pump();
  }

  /// Quello che manda il runner quando si trascinano file sulla finestra.
  Future<void> drop(WidgetTester tester, String method, [Object? arguments]) =>
      tester.binding.defaultBinaryMessenger.handlePlatformMessage(
        'megaplayer/drop',
        const StandardMethodCodec().encodeMethodCall(MethodCall(method, arguments)),
        (_) {},
      );

  testWidgets('M3U e file trascinati, con la mascotte che lo dice', (tester) async {
    store.m3uFiles['/Liste/Sera.m3u8'] = [const Track(ref: '/Musica/giro.sid', name: '/Musica/giro.sid', subsong: 1)];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(const MethodChannel('megaplayer/files'), (call) async {
      if (call.method == 'openFiles') return ['/Liste/Sera.m3u8'];
      return call.method == 'saveFile' ? '/Liste/Copia.m3u8' : null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('megaplayer/files'), null));
    await open(tester, tab: 'PLAYLIST');
    await choose(tester, find.text('Importa M3U…'));
    expect(find.text('Playlist «Sera» con 1 brano'), findsOneWidget);
    expect(find.text('SERA'), findsOneWidget);

    await tester.tap(find.text('ESPORTA M3U…'));
    await tester.pump();
    expect(store.exported['/Liste/Copia.m3u8'], hasLength(1));
    expect(find.text('Salvata in Copia.m3u8'), findsOneWidget);

    await drop(tester, 'enter');
    await tester.pump();
    expect(find.text('Lascia qui i file'), findsOneWidget);
    await drop(tester, 'drop', [FakeCore.archive]);
    await tester.pump();
    expect(find.text('Lascia qui i file'), findsNothing);
    expect(controller.tracks, hasLength(6));
    expect(find.text('Aggiunti alla coda 6 brani'), findsOneWidget);
  });

  testWidgets('cartelle, sottobrani e tutto in coda, con la mascotte che lo dice', (tester) async {
    await open(tester);
    await tester.tap(find.textContaining('giro.sid'));
    await tester.pump();
    expect(controller.currentTrack!.ref, '/Musica/giro.sid');

    await tester.tap(find.byTooltip('Mostra i 3 sottobrani'));
    await tester.pump();
    expect(find.text('Sottobrano 3'), findsOneWidget);
    expect(find.text('1:30'), findsOneWidget);
    await tester.tap(find.text('Sottobrano 2'));
    await tester.pump();
    expect(controller.currentTrack!.subsong, 1);

    await tester.tap(find.text('Amiga'));
    await tester.pump();
    await tester.tap(find.text('Jesper Kyd'));
    await tester.pump();
    await tester.tap(find.text('TUTTO IN CODA'));
    await tester.pump();
    expect(controller.tracks.map((t) => t.fileName), containsAll(['nebula.mod', 'heroes.med']));
    expect(find.text('Aggiunti alla coda 2 brani'), findsOneWidget);

    await tester.tap(find.byTooltip('Cartella superiore'));
    await tester.pump();
    expect(controller.library.path, ['Amiga']);
    await tester.tap(find.text('Musica'));
    await tester.pump();
    expect(controller.library.path, isEmpty);

    // La mascotte smette di parlare dopo qualche secondo.
    await tester.pump(const Duration(seconds: 5));
    expect(find.text('Aggiunti alla coda 2 brani'), findsNothing);
  });

  testWidgets('filtro per formato dal menu a comparsa', (tester) async {
    await open(tester);
    await tester.tap(find.text('Tutti'));
    await tester.pump();
    await choose(tester, find.text('MED'));
    expect(controller.library.query.formats, {'MED'});
    expect(find.textContaining('heroes.med'), findsOneWidget);
    expect(find.text('Amiga'), findsNothing, reason: 'con un filtro niente cartelle');
  });

  testWidgets('nuova playlist dalla linguetta, un brano dentro, poi eliminata', (tester) async {
    await open(tester);
    await tester.tap(find.text('PLAYLIST'));
    await tester.pump();
    await choose(tester, find.text('Nuova playlist…'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'Preferiti');
    await tester.tap(find.text('Crea'));
    await tester.pumpAndSettle();
    expect(controller.playlists.single.name, 'Preferiti');
    expect(find.text('Playlist — Preferiti'), findsOneWidget);

    await tester.tap(find.text('LIBRERIA'));
    await tester.pump();
    await tester.tap(find.byTooltip('Aggiungi a una playlist').first);
    await tester.pump();
    await choose(tester, find.text('Preferiti').last);
    expect(controller.playlistTracks(controller.playlists.single.id).single.fileName, 'giro.sid');
    expect(find.text('Aggiunto a «Preferiti»'), findsOneWidget);

    await tester.tap(find.text('PLAYLIST'));
    await tester.pump();
    await choose(tester, find.text('Preferiti').last);
    await tester.tap(find.text('ELIMINA…'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Elimina'));
    await tester.pumpAndSettle();
    expect(controller.playlists, isEmpty);
    expect(find.text('Coda'), findsOneWidget, reason: 'si torna alla coda');
  });

  testWidgets('la coda: sottobrano e togliere dal menu', (tester) async {
    controller.addPaths([FakeCore.archive]);
    await open(tester, tab: 'CODA');
    await tester.tap(find.byTooltip('Altro').at(1)); // jt.ingame
    await tester.pump();
    await choose(tester, find.text('Sottobrano…'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, '3');
    await tester.tap(find.text('Scegli'));
    await tester.pumpAndSettle();
    expect(controller.tracks[1].subsong, 2);

    await tester.tap(find.byTooltip('Altro').first);
    await tester.pump();
    await choose(tester, find.text('Togli dalla coda'));
    expect(controller.tracks, hasLength(5));
  });

  testWidgets('canali: zittire, solo e tutti di nuovo', (tester) async {
    controller.addPaths([FakeCore.archive]);
    await open(tester, tab: 'CODA');
    core.calls.clear();
    await tester.tap(find.byTooltip('Voce 2 · destra · clic per zittirlo'));
    await tester.pump();
    expect(core.calls, ['mute 1']);
    expect(find.byTooltip('Voce 2 · destra · clic per riaccenderlo'), findsOneWidget);

    await tester.tap(find.byTooltip('Senti solo questo canale (con gli altri in solo)').at(3));
    await tester.pump();
    expect(controller.soloChannels, {3});
    expect([for (var i = 0; i < 4; i++) controller.channelAudible(i)], [false, false, false, true]);

    await tester.tap(find.text('TUTTI'));
    await tester.pump();
    expect(controller.mutedChannels, isEmpty);
    expect(controller.soloChannels, isEmpty);
    expect(core.calls.where((c) => c.startsWith('unmute')), hasLength(3));
  });

  testWidgets('aggiornamento: l\'avviso all\'avvio, la mascotte e il menu', (tester) async {
    final updater = FakeUpdater(found: newRelease);
    final updates = Updates(updater, quit: () async {}, currentVersion: '0.1.0');
    controller = PlayerController(core, store: store, updates: updates)
      ..restore()
      ..setSkin(Skin.retro);
    await updates.check();
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.pumpAndSettle();
    expect(find.text('È arrivato megaplayer 0.2.0'), findsOneWidget);
    await tester.tap(find.text('Più tardi'));
    await tester.pumpAndSettle();

    expect(find.text('C\'è megaplayer 0.2.0! Lo installi da «Aggiorna!» nel menu.'), findsOneWidget);
    await tester.tap(find.text('Aggiorna!'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Installa e riavvia'));
    await tester.pump();
    // Lo dicono l'avviso e la mascotte.
    expect(find.text('Scarico l\'aggiornamento… 42%'), findsNWidgets(2));
    updater.finishDownload();
    await tester.pump();
    expect(updater.launched, hasLength(1));
  });

  testWidgets('aggiornamenti: dal menu, quando non ce ne sono', (tester) async {
    controller = PlayerController(core, store: store, updates: Updates(FakeUpdater(), quit: () async {}))
      ..restore()
      ..setSkin(Skin.retro);
    await open(tester, tab: 'CODA');
    await tester.tap(find.text('Aggiornamenti…'));
    await tester.pumpAndSettle();
    expect(find.text('Nessun aggiornamento'), findsOneWidget);
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(find.text('Aggiorna!'), findsNothing);
  });

  testWidgets('impostazioni: la cartella delle ROM e il Sound Canvas per i MIDI', (tester) async {
    controller.setSkin(Skin.retro);
    Object? asked;
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(const MethodChannel('megaplayer/files'), (call) async {
      asked = call.arguments;
      return call.method == 'openFolder' ? '/Roms/Roland' : null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('megaplayer/files'), null));
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.text('Impostazioni…'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.textContaining('Nessuna ROM trovata'), findsOneWidget);

    await tester.tap(find.text('Scegli…').first);
    await tester.pump();
    expect((asked as Map)['message'], contains('ROM'));
    expect(find.textContaining('Moduli pronti: SC-55mkII, SC-88Pro, MT-32 1.x (sperimentale)'), findsOneWidget);
    expect(find.text('Automatico (SC-55mkII)'), findsOneWidget);

    await tester.tap(find.text('SC-88Pro'));
    await tester.pump();
    expect(core.rolandDevice, 'SC-88Pro');
    expect(store.settings['midi.roland'], 'SC-88Pro');
    expect(store.settings['midi.roms'], '/Roms/Roland');
    await tester.tap(find.text('Fatto'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.text('Impostazioni'), findsNothing);
  });

  testWidgets('impostazioni: i MIDI senza ROM sul SoundFont incluso, su uno scelto o in FM', (tester) async {
    controller.setSkin(Skin.retro);
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(const MethodChannel('megaplayer/files'), (call) async {
      return call.method == 'openFiles' && (call.arguments as Map)['single'] == true ? ['/SoundFont/Arachno.sf2'] : null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('megaplayer/files'), null));
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.text('Impostazioni…'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.textContaining('i MIDI suonano con il SoundFont GeneralUser GS'), findsOneWidget);

    await tester.tap(find.text('Scegli…').last);
    await tester.pump();
    expect(core.currentSoundFont, '/SoundFont/Arachno.sf2');
    expect(find.text('Il SoundFont Arachno'), findsOneWidget);
    expect(store.settings['midi.soundfont'], 'custom');

    await tester.tap(find.text('I chip FM, come una Sound Blaster'));
    await tester.pump();
    expect(core.currentSoundFont, '');
    expect(find.textContaining('i MIDI suonano sui chip FM'), findsOneWidget);

    await tester.tap(find.text('GeneralUser GS, incluso in megaplayer'));
    await tester.pump();
    expect(core.currentSoundFont, '/App/soundfonts/GeneralUser-GS.sf2');
    expect(store.settings['midi.soundfont'], 'included');
  });

  testWidgets('esportazione: formato, MegaSound, finestra Salva e avanzamento', (tester) async {
    controller.setSkin(Skin.retro);
    Object? asked;
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(const MethodChannel('megaplayer/files'), (call) async {
      asked = call.arguments;
      return call.method == 'saveFile' ? '/Export/brano.mp3' : null;
    });
    addTearDown(() => tester.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('megaplayer/files'), null));
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(2);
    controller.setMegaSound(0.5);
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.text('Esporta…').first);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.text('MP3, alta qualità (VBR)'), findsOneWidget);
    await tester.tap(find.text('Con la MegaSound al 50%'));
    await tester.pump();
    await tester.tap(find.text('Esporta…').last);
    await tester.pump();
    expect((asked as Map)['name'], 'jt.title.mp3');
    expect(core.exports.single, 'jt.title 0 /Export/brano.mp3 0 [] "jt.title"');
    expect(find.text('Esporto… 0:00 di 3:04'), findsOneWidget);

    core.exportState = const ExportStatus(seconds: 184, total: 184);
    controller.refresh();
    await tester.pump();
    expect(find.text('Salvato in /Export/brano.mp3'), findsOneWidget);
    await tester.tap(find.text('Fatto'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.text('Esporta il brano'), findsNothing);
    expect(controller.exportJob, isNull);
  });
}
