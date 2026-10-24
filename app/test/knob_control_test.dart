import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/app.dart';
import 'package:megaplayer/player/megasound.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fonts.dart';

void main() {
  setUpAll(loadAppFonts);

  Future<PlayerController> pumpApp(WidgetTester tester, Skin skin) async {
    tester.view.physicalSize = const Size(1440, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final controller = PlayerController(FakeCore())..setSkin(skin);
    await tester.pumpWidget(MegaplayerApp(controller: controller));
    return controller;
  }

  for (final skin in Skin.values) {
    testWidgets('la manopola gira trascinando e con le frecce (${skin.name})', (tester) async {
      final controller = await pumpApp(tester, skin);
      final knob = find.bySemanticsLabel('Manopola MegaSound');
      expect(knob, findsOneWidget);

      // 100 px verso l'alto, a piccoli passi: metà corsa.
      final gesture = await tester.startGesture(tester.getCenter(knob));
      for (var i = 0; i < 10; i++) {
        await gesture.moveBy(const Offset(0, -10));
      }
      await gesture.up();
      await tester.pump();
      expect(controller.megaSound, closeTo(0.5, 0.06));

      await tester.sendKeyEvent(LogicalKeyboardKey.end);
      await tester.pump();
      expect(controller.megaSound, 1.0);
      await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
      await tester.pump();
      expect(controller.megaSound, closeTo(0.95, 1e-9));
      final character = megaSoundCharacter(0.95);
      expect(find.text(skin == Skin.retro ? character.toUpperCase() : character), findsOneWidget);
      // Lascia scadere l'attesa del doppio clic.
      await tester.pump(const Duration(seconds: 1));
    });
  }

  test('gli effetti entrano nell\'ordine della catena', () {
    double level(String name, double amount) =>
        megaSoundStages.firstWhere((s) => s.name == name).level(amount);
    expect(level('Compressore', 0.1), greaterThan(0));
    // Lo Spazio è pieno già al primo tratto.
    expect(level('Spazio', 0.15), 1);
    expect(level('Riverbero', 0.1), 0);
    expect(level('Clipper', 0.85), closeTo(0.5, 1e-9));
    expect(level('Saturazione', 0.5), closeTo(0.5, 1e-9));
  });
}
