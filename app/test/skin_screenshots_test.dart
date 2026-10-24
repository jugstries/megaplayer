// Disegna le due skin con il core finto e salva le immagini in
// build/screenshots, per controllarle a occhio: non è un confronto con
// immagini di riferimento.

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fonts.dart';

void main() {
  autoUpdateGoldenFiles = true;

  setUpAll(loadAppFonts);

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
