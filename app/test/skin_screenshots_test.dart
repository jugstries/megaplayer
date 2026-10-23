// Disegna le due skin con il core finto e salva le immagini in
// build/screenshots, per controllarle a occhio: non è un confronto con
// immagini di riferimento.

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';

Future<void> _loadFonts() async {
  Future<void> load(String family, Future<ByteData> data) => (FontLoader(family)..addFont(data)).load();

  await load('Sora', rootBundle.load('assets/fonts/Sora-Variable.ttf'));
  await load('JetBrainsMono', rootBundle.load('assets/fonts/JetBrainsMono-Variable.ttf'));
  await load('PixelifySans', rootBundle.load('assets/fonts/PixelifySans-Variable.ttf'));
  await load('Silkscreen', rootBundle.load('assets/fonts/Silkscreen-Regular.ttf'));
  await load('VT323', rootBundle.load('assets/fonts/VT323-Regular.ttf'));

  // Le icone Material stanno nella cache dell'SDK, accanto a flutter_tester.
  final artifacts = File(Platform.resolvedExecutable).parent.parent.parent;
  final icons = File('${artifacts.path}/material_fonts/MaterialIcons-Regular.otf');
  if (icons.existsSync()) {
    await load('MaterialIcons', Future.value(ByteData.sublistView(icons.readAsBytesSync())));
  }
}

void main() {
  autoUpdateGoldenFiles = true;

  setUpAll(_loadFonts);

  Future<void> shoot(WidgetTester tester, PlayerController controller, String name) async {
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    await tester.pump();
    await expectLater(find.byType(MegaplayerApp), matchesGoldenFile('../build/screenshots/$name.png'));
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
    await shoot(tester, playingTitle(), 'moderna');
  });

  testWidgets('skin moderna, nessun brano', (tester) async {
    await shoot(tester, PlayerController(FakeCore()), 'moderna-vuota');
  });

  testWidgets('skin retrò', (tester) async {
    await shoot(tester, playingTitle()..setSkin(Skin.retro), 'retro');
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
