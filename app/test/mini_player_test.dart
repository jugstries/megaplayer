// Il miniplayer dal lato dell'app: lo stato che va al runner sul canale
// "megaplayer/tray" e i comandi che tornano, con il core e il database finti.

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/core/models.dart';
import 'package:megaplayer/player/mini_player.dart';
import 'package:megaplayer/player/player_controller.dart';

import 'fake_core.dart';
import 'fake_library.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('megaplayer/tray');
  final messenger = TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;

  late FakeCore core;
  late FakeLibraryStore store;
  late PlayerController controller;
  late List<Map<Object?, Object?>> sent;
  late MiniPlayer miniPlayer;

  setUp(() {
    core = FakeCore();
    store = FakeLibraryStore();
    controller = PlayerController(core, store: store, miniPlayerSupported: true)..restore();
    sent = [];
    messenger.setMockMethodCallHandler(channel, (call) async {
      if (call.method == 'update') sent.add(call.arguments as Map<Object?, Object?>);
      return null;
    });
    miniPlayer = MiniPlayer(controller);
  });

  tearDown(() {
    miniPlayer.dispose();
    messenger.setMockMethodCallHandler(channel, null);
  });

  /// Quello che manda il runner quando si usa il miniplayer.
  Future<void> command(String method, [Object? arguments]) async {
    await messenger.handlePlatformMessage(
      'megaplayer/tray',
      const StandardMethodCodec().encodeMethodCall(MethodCall(method, arguments)),
      (_) {},
    );
    await pumpEventQueue();
  }

  test('si accende e si spegne con l\'impostazione, che resta da un avvio all\'altro', () async {
    expect(sent, [
      {'visible': false},
    ]);

    controller.setMiniPlayer(true);
    await pumpEventQueue();
    expect(sent.last['visible'], isTrue);
    expect(sent.last['title'], '');
    expect(sent.last['canPlay'], isFalse);
    expect(sent.last['labels'], containsPair('open', 'Apri megaplayer'));
    expect(store.settings['app.miniplayer'], 'on');
    expect(PlayerController(core, store: store).restoreMiniPlayer(), isTrue);

    controller.setMiniPlayer(false);
    await pumpEventQueue();
    expect(sent.last, {'visible': false});
    expect(store.settings['app.miniplayer'], 'off');
    expect(PlayerController(core, store: store).restoreMiniPlayer(), isFalse);
  });

  test('il titolo segue il brano, e allo stesso stato non si rimanda niente', () async {
    controller.setMiniPlayer(true);
    controller.addPaths([FakeCore.archive]);
    await pumpEventQueue();
    expect(sent.last['title'], 'cust.title');
    expect(sent.last['playing'], isTrue);
    expect(sent.last['canPrevious'], isTrue);
    expect(sent.last['canNext'], isTrue);

    final before = sent.length;
    controller
      ..refresh()
      ..refresh();
    await pumpEventQueue();
    expect(sent, hasLength(before));

    // A fine brano il core passa al successivo, e il titolo cambia.
    core.state = PlaybackState.ended;
    controller.refresh();
    await pumpEventQueue();
    expect(sent.last['title'], 'jt.ingame');

    controller.playIndex(controller.tracks.length - 1);
    await pumpEventQueue();
    expect(sent.last['canNext'], isFalse);
  });

  test('i comandi del miniplayer arrivano al player', () async {
    controller.setMiniPlayer(true);
    controller.addPaths([FakeCore.archive]);

    await command('togglePause');
    expect(core.state, PlaybackState.paused);
    expect(sent.last['playing'], isFalse);
    await command('togglePause');
    expect(core.state, PlaybackState.playing);

    await command('next');
    expect(controller.currentIndex, 1);
    expect(sent.last['title'], 'jt.ingame');
    await command('previous');
    expect(controller.currentIndex, 0);

    await command('setVolume', 0.25);
    expect(controller.volume, 0.25);
    expect(sent.last['volume'], 0.25);
    expect(store.settings['app.volume'], '0.25');
  });

  test('da fermo, play fa partire la coda', () async {
    controller.setMiniPlayer(true);
    controller.addPaths([FakeCore.archive]);
    core.stop();
    controller.refresh();
    await command('togglePause');
    expect(core.state, PlaybackState.playing);
  });
}

extension on PlayerController {
  /// Il miniplayer come lo ritrova un nuovo avvio.
  bool restoreMiniPlayer() {
    restore();
    return miniPlayer;
  }
}
