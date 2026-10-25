// Disegna le due skin con il core finto e salva le immagini in
// build/screenshots, per controllarle a occhio: non è un confronto con
// immagini di riferimento.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fake_library.dart';
import 'fonts.dart';

void main() {
  autoUpdateGoldenFiles = true;

  setUpAll(loadAppFonts);

  Future<void> shoot(
    WidgetTester tester,
    PlayerController controller,
    String name, {
    String? open,
    Size size = const Size(1440, 900),
  }) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.pump();
    if (open != null) {
      await tester.tap(find.text(open).first);
      await tester.pump();
    }
    await expectLater(find.byType(MegaplayerApp), matchesGoldenFile('../build/screenshots/$name.png'));
  }

  /// Una libreria finta già letta, con la coda di Stormlord che suona.
  PlayerController withLibrary(FakeLibraryStore store) {
    store.rootPath = '/Users/me/Musica/Moduli';
    final core = FakeCore();
    final controller = PlayerController(core, store: store)..restore();
    store.finishScan([
      libraryItem('', 'Compilation_I.sid', platform: 'c64', format: 'PSID', title: 'Compilation I', subsongs: 12),
      libraryItem('', 'Starball.sid', platform: 'c64', format: 'PSID', title: 'Starball', durations: [212]),
      for (var i = 0; i < 40; i++)
        libraryItem('Music-AmigaModsAnthology-1of4-Psychodk.mcsteam.iso/Modules/Artist$i', 'mod.track$i', durations: [120.0 + i]),
      libraryItem('Stormlord.lha/Stormlord', 'jt.title', format: 'Jeroen Tel', durations: [184.5]),
      libraryItem('TFMX Professional/Apidya', 'mdat.apidya', format: 'TFMX', subsongs: 9),
      libraryItem('Thunder Force IV.zip', 'tf4-01.vgz', platform: 'sega', format: 'VGM', title: 'Lightning Strikes Again'),
    ]);
    controller.refresh();
    controller.addPaths([FakeCore.archive]);
    controller.playIndex(2);
    core.position = 72;
    controller.refresh();
    return controller;
  }

  PlayerController playingTitle() {
    final core = FakeCore();
    final controller = PlayerController(core)..addPaths([FakeCore.archive]);
    controller.playIndex(2);
    core.position = 72;
    controller.refresh();
    return controller;
  }

  testWidgets('skin moderna', (tester) async {
    await shoot(tester, playingTitle()..setMegaSound(0.7), 'moderna');
  });

  testWidgets('skin moderna, confronto A/B', (tester) async {
    final controller = playingTitle()
      ..setMegaSound(0.45)
      ..toggleMegaSoundCompare();
    await shoot(tester, controller, 'moderna-confronto');
  });

  testWidgets('skin moderna, nessun brano', (tester) async {
    await shoot(tester, PlayerController(FakeCore()), 'moderna-vuota');
  });

  testWidgets('skin moderna, libreria', (tester) async {
    await shoot(tester, withLibrary(FakeLibraryStore()), 'moderna-libreria', open: 'Libreria');
  });

  testWidgets('skin moderna, dentro una cartella dal nome lungo', (tester) async {
    final controller = withLibrary(FakeLibraryStore())
      ..library.openFolder('Music-AmigaModsAnthology-1of4-Psychodk.mcsteam.iso/Modules/Artist7');
    await shoot(tester, controller, 'moderna-cartella', open: 'Libreria', size: const Size(1180, 800));
  });

  testWidgets('skin moderna, ricerca nella libreria', (tester) async {
    final controller = withLibrary(FakeLibraryStore())..library.togglePlatform('c64');
    await shoot(tester, controller, 'moderna-ricerca', open: 'Libreria');
  });

  testWidgets('skin moderna, libreria da scegliere', (tester) async {
    final controller = PlayerController(FakeCore(), store: FakeLibraryStore())..restore();
    await shoot(tester, controller, 'moderna-libreria-vuota', open: 'Libreria');
  });

  testWidgets('skin moderna, playlist', (tester) async {
    final controller = withLibrary(FakeLibraryStore());
    controller.createPlaylist('Preferiti', entries: [
      controller.tracks[2],
      controller.tracks[1].withSubsong(2),
      controller.tracks[3],
    ]);
    controller.createPlaylist('Da sentire');
    await shoot(tester, controller, 'moderna-playlist', open: 'Preferiti');
  });

  testWidgets('skin moderna, finestra stretta', (tester) async {
    await shoot(tester, withLibrary(FakeLibraryStore()), 'moderna-stretta', size: const Size(1000, 700));
  });

  testWidgets('skin retrò', (tester) async {
    await shoot(tester, playingTitle()..setSkin(Skin.retro)..setMegaSound(0.7), 'retro');
  });

  testWidgets('skin retrò, sottobrani', (tester) async {
    final core = FakeCore();
    final controller = PlayerController(core)
      ..addPaths([FakeCore.archive])
      ..setSkin(Skin.retro);
    controller.playIndex(1);
    controller.selectSubsong(1);
    core.position = 12;
    controller.refresh();
    await shoot(tester, controller, 'retro-sottobrani');
  });
}
