// La libreria, la coda e le playlist della skin moderna, usate come un
// utente: clic, menu e finestre di dialogo, con il core e il database finti.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fake_library.dart';
import 'fonts.dart';

void main() {
  late FakeCore core;
  late FakeLibraryStore store;
  late PlayerController controller;

  setUpAll(loadAppFonts);

  setUp(() {
    core = FakeCore();
    store = FakeLibraryStore()..rootPath = '/Musica';
    controller = PlayerController(core, store: store)..restore();
    store.finishScan([
      libraryItem('', 'giro.sid', platform: 'c64', format: 'PSID', subsongs: 3, durations: [60, 90, null]),
      libraryItem('Amiga/Jesper Kyd', 'nebula.mod', title: 'Nebula'),
      libraryItem('Amiga/Jesper Kyd', 'heroes.med', format: 'MED'),
    ]);
    controller.refresh();
  });

  Future<void> open(WidgetTester tester) async {
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.text('Libreria'));
    await tester.pump();
  }

  testWidgets('cartelle, brani e sottobrani dalla libreria', (tester) async {
    await open(tester);
    expect(find.text('Amiga'), findsOneWidget);

    // Un clic sul brano lo suona; i sottobrani si aprono dal loro numero.
    await tester.tap(find.text('giro.sid'));
    await tester.pump();
    expect(controller.currentTrack!.ref, '/Musica/giro.sid');
    await tester.tap(find.text('3 sottobrani'));
    await tester.pump();
    expect(find.text('Sottobrano 2'), findsOneWidget);
    expect(find.text('1:30'), findsOneWidget);
    await tester.tap(find.text('Sottobrano 2'));
    await tester.pump();
    expect(controller.currentTrack!.subsong, 1);

    await tester.tap(find.text('Amiga'));
    await tester.pump();
    await tester.tap(find.text('Jesper Kyd'));
    await tester.pump();
    expect(find.text('Nebula'), findsOneWidget);
    await tester.tap(find.byTooltip('Cartella superiore'));
    await tester.pump();
    expect(controller.library.path, ['Amiga']);
    await tester.tap(find.byIcon(Icons.home_rounded));
    await tester.pump();
    expect(controller.library.path, isEmpty);
    // Anche un altro clic su «Libreria» torna alla radice.
    controller.library.openFolder('Amiga/Jesper Kyd');
    await tester.tap(find.text('Libreria').first); // la voce nella barra laterale
    await tester.pump();
    expect(controller.library.path, isEmpty);
    controller.library.openFolder('Amiga/Jesper Kyd');
    await tester.pump();
    await tester.tap(find.text('Tutto in coda'));
    await tester.pump();
    expect(controller.tracks.map((t) => t.fileName), containsAll(['nebula.mod', 'heroes.med']));
    expect(find.text('Aggiunti alla coda 2 brani'), findsOneWidget);
  });

  testWidgets('filtro per piattaforma', (tester) async {
    await open(tester);
    await tester.tap(find.text('c64'));
    await tester.pump();
    expect(find.text('Amiga'), findsNothing, reason: 'con un filtro niente cartelle');
    expect(find.text('1 brano trovati'), findsOneWidget);
    expect(find.text('giro.sid'), findsOneWidget);
  });

  testWidgets('nuova playlist dalla barra laterale, e un brano dentro', (tester) async {
    await open(tester);
    await tester.tap(find.byTooltip('Nuova playlist'));
    await tester.pumpAndSettle();
    await tester.enterText(find.byType(TextField).last, 'Preferiti');
    await tester.tap(find.text('Crea'));
    await tester.pumpAndSettle();
    expect(controller.playlists.single.name, 'Preferiti');
    expect(find.text('PLAYLIST'), findsWidgets);

    await tester.tap(find.text('Libreria'));
    await tester.pump();
    await tester.tap(find.byTooltip('Aggiungi a una playlist').first);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Preferiti').last);
    await tester.pumpAndSettle();
    expect(controller.playlistTracks(controller.playlists.single.id).single.fileName, 'giro.sid');
  });

  testWidgets('la coda: sottobrano e togliere dal menu', (tester) async {
    controller.addPaths([FakeCore.archive]);
    await open(tester);
    await tester.tap(find.byTooltip('Altro').at(1)); // jt.ingame
    await tester.pumpAndSettle();
    await tester.tap(find.text('Sottobrano…'));
    await tester.pumpAndSettle();
    // Fuori dalla libreria e non in riproduzione: il numero si scrive.
    await tester.enterText(find.byType(TextField).last, '3');
    await tester.tap(find.text('Scegli'));
    await tester.pumpAndSettle();
    expect(controller.tracks[1].subsong, 2);

    await tester.tap(find.byTooltip('Altro').first);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Togli dalla coda'));
    await tester.pumpAndSettle();
    expect(controller.tracks, hasLength(5));
  });

  testWidgets('canali: zittire, solo e tutti di nuovo', (tester) async {
    controller.addPaths([FakeCore.archive]);
    // I canali stanno in «In riproduzione», la vista di partenza.
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    core.calls.clear();
    await tester.tap(find.byTooltip('Voce 2 · destra · clic per zittirlo'));
    await tester.pump();
    expect(core.calls, ['mute 1']);
    expect(find.byTooltip('Voce 2 · destra · clic per riaccenderlo'), findsOneWidget);

    await tester.tap(find.byTooltip('Senti solo questo canale (con gli altri in solo)').at(3));
    await tester.pump();
    expect(controller.soloChannels, {3});
    expect([for (var i = 0; i < 4; i++) controller.channelAudible(i)], [false, false, false, true]);

    await tester.tap(find.text('Tutti'));
    await tester.pump();
    expect(controller.mutedChannels, isEmpty);
    expect(controller.soloChannels, isEmpty);
    expect(core.calls.where((c) => c.startsWith('unmute')), hasLength(3));
  });
}
