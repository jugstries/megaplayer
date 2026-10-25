// Libreria, coda e playlist della skin retrò, usate come un utente: clic
// sulle linguette, menu a comparsa e finestre di dialogo, con il core e il
// database finti.

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
}
