import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/core/models.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';

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

    controller.playIndex(5);
    core.state = PlaybackState.ended;
    controller.refresh();
    expect(controller.currentIndex, 5);
    expect(core.playing, isNull);
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
}
