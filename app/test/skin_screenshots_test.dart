// Disegna le due skin con il core finto e salva le immagini in
// build/screenshots, per controllarle a occhio: non è un confronto con
// immagini di riferimento.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/player_controller.dart';
import 'package:megaplayer/update/updates.dart';

import 'fake_core.dart';
import 'fake_library.dart';
import 'fake_updates.dart';
import 'fonts.dart';

void main() {
  autoUpdateGoldenFiles = true;

  setUpAll(loadAppFonts);

  Future<void> shoot(
    WidgetTester tester,
    PlayerController controller,
    String name, {
    List<String> taps = const [],
    Size size = const Size(1440, 900),
    bool dialog = false,
  }) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.pump();
    for (final text in taps) {
      await tester.tap(find.text(text).first);
      // Le voci dei menu agiscono al frame dopo la chiusura.
      await tester.pump();
      await tester.pump();
    }
    // Finestre di dialogo: fino alla fine dell'animazione.
    if (dialog) await tester.pump(const Duration(milliseconds: 400));
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

  PlayerController playingTitle({Updates? updates}) {
    final core = FakeCore();
    final controller = PlayerController(core, updates: updates)..addPaths([FakeCore.archive]);
    controller.playIndex(2);
    core.position = 72;
    controller.refresh();
    return controller;
  }

  testWidgets('skin moderna', (tester) async {
    await shoot(tester, playingTitle()..setMegaSound(0.7), 'moderna');
  });

  testWidgets('skin moderna, canali zittiti e in solo', (tester) async {
    final controller = playingTitle()
      ..toggleChannelMute(1)
      ..toggleChannelSolo(2)
      ..toggleChannelSolo(3);
    await shoot(tester, controller, 'moderna-canali');
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
    await shoot(tester, withLibrary(FakeLibraryStore()), 'moderna-libreria', taps: ['Libreria']);
  });

  testWidgets('skin moderna, dentro una cartella dal nome lungo', (tester) async {
    final controller = withLibrary(FakeLibraryStore())
      ..library.openFolder('Music-AmigaModsAnthology-1of4-Psychodk.mcsteam.iso/Modules/Artist7');
    await shoot(tester, controller, 'moderna-cartella', taps: ['Libreria'], size: const Size(1180, 800));
  });

  testWidgets('skin moderna, ricerca nella libreria', (tester) async {
    final controller = withLibrary(FakeLibraryStore())..library.togglePlatform('c64');
    await shoot(tester, controller, 'moderna-ricerca', taps: ['Libreria']);
  });

  testWidgets('skin moderna, libreria da scegliere', (tester) async {
    final controller = PlayerController(FakeCore(), store: FakeLibraryStore())..restore();
    await shoot(tester, controller, 'moderna-libreria-vuota', taps: ['Libreria']);
  });

  testWidgets('skin moderna, playlist', (tester) async {
    final controller = withLibrary(FakeLibraryStore());
    controller.createPlaylist('Preferiti', entries: [
      controller.tracks[2],
      controller.tracks[1].withSubsong(2),
      controller.tracks[3],
    ]);
    controller.createPlaylist('Da sentire');
    await shoot(tester, controller, 'moderna-playlist', taps: ['Preferiti']);
  });

  testWidgets('skin moderna, finestra stretta', (tester) async {
    await shoot(tester, withLibrary(FakeLibraryStore()), 'moderna-stretta', size: const Size(1000, 700));
  });

  testWidgets('skin moderna, aggiornamento annunciato', (tester) async {
    final updates = Updates(FakeUpdater(found: newRelease), quit: () async {}, currentVersion: '0.1.0');
    await updates.check();
    await shoot(tester, playingTitle(updates: updates), 'moderna-aggiornamento', dialog: true);
  });

  testWidgets('skin moderna, aggiornamento che si scarica', (tester) async {
    final updates = Updates(FakeUpdater(found: newRelease), quit: () async {}, currentVersion: '0.1.0');
    await updates.check();
    updates
      ..markAnnounced()
      ..install();
    await shoot(tester, playingTitle(updates: updates), 'moderna-scaricamento', taps: ['Scarico… 42%'], dialog: true);
  });

  testWidgets('skin retrò', (tester) async {
    await shoot(tester, playingTitle()..setSkin(Skin.retro)..setMegaSound(0.7), 'retro');
  });

  testWidgets('skin retrò, coda con le linguette', (tester) async {
    await shoot(tester, withLibrary(FakeLibraryStore())..setSkin(Skin.retro), 'retro-coda');
  });

  testWidgets('skin retrò, libreria', (tester) async {
    final controller = withLibrary(FakeLibraryStore())..setSkin(Skin.retro);
    await shoot(tester, controller, 'retro-libreria', taps: ['LIBRERIA']);
  });

  testWidgets('skin retrò, menu del filtro per formato', (tester) async {
    final controller = withLibrary(FakeLibraryStore())
      ..setSkin(Skin.retro)
      ..library.toggleFormat('PSID');
    await shoot(tester, controller, 'retro-filtro', taps: ['LIBRERIA', 'PSID']);
  });

  testWidgets('skin retrò, playlist', (tester) async {
    final controller = withLibrary(FakeLibraryStore())..setSkin(Skin.retro);
    controller.createPlaylist('Preferiti', entries: [
      controller.tracks[2],
      controller.tracks[1].withSubsong(2),
      controller.tracks[3],
    ]);
    await shoot(tester, controller, 'retro-playlist', taps: ['PLAYLIST', 'Preferiti']);
  });

  testWidgets('skin retrò, libreria da scegliere', (tester) async {
    final controller = PlayerController(FakeCore(), store: FakeLibraryStore())
      ..restore()
      ..setSkin(Skin.retro);
    await shoot(tester, controller, 'retro-libreria-vuota', taps: ['LIBRERIA']);
  });

  testWidgets('skin retrò, finestra stretta con la libreria', (tester) async {
    final controller = withLibrary(FakeLibraryStore())..setSkin(Skin.retro);
    await shoot(tester, controller, 'retro-stretta', taps: ['LIBRERIA'], size: const Size(1000, 760));
  });

  testWidgets('skin retrò, canali zittiti e in solo', (tester) async {
    final controller = playingTitle()
      ..setSkin(Skin.retro)
      ..toggleChannelMute(1)
      ..toggleChannelSolo(2)
      ..toggleChannelSolo(3);
    await shoot(tester, controller, 'retro-canali');
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

  testWidgets('skin retrò, aggiornamento che si scarica', (tester) async {
    final updates = Updates(FakeUpdater(found: newRelease), quit: () async {}, currentVersion: '0.1.0');
    await updates.check();
    updates
      ..markAnnounced()
      ..install();
    await shoot(tester, playingTitle(updates: updates)..setSkin(Skin.retro), 'retro-aggiornamento',
        taps: ['Aggiorna!'], dialog: true);
  });

  /// Con le ROM trovate e l'SC-88Pro scelto.
  PlayerController withRoms(PlayerController controller) => controller
    ..setRomFolder('/Roms/Roland')
    ..setRolandDevice('SC-88Pro');

  testWidgets('skin moderna, impostazioni dei moduli Roland', (tester) async {
    final controller = withRoms(withLibrary(FakeLibraryStore()));
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.byTooltip('Impostazioni'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    await expectLater(find.byType(MegaplayerApp), matchesGoldenFile('../build/screenshots/moderna-impostazioni.png'));
  });

  testWidgets('skin retrò, impostazioni dei moduli Roland', (tester) async {
    final controller = withRoms(withLibrary(FakeLibraryStore()))..setSkin(Skin.retro);
    await shoot(tester, controller, 'retro-impostazioni', taps: ['Impostazioni…'], dialog: true);
  });

  testWidgets('skin moderna, esportazione', (tester) async {
    final controller = playingTitle()
      ..setMegaSound(0.6)
      ..toggleChannelMute(1);
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.tap(find.byTooltip('Esporta in WAV o MP3'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    await expectLater(find.byType(MegaplayerApp), matchesGoldenFile('../build/screenshots/moderna-esporta.png'));
  });

  testWidgets('skin retrò, esportazione', (tester) async {
    final controller = playingTitle()
      ..setSkin(Skin.retro)
      ..setMegaSound(0.6)
      ..toggleChannelMute(1);
    await shoot(tester, controller, 'retro-esporta', taps: ['Esporta…'], dialog: true);
  });
}
