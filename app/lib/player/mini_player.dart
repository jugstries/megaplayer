import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'player_controller.dart';

// I testi del miniplayer. Li mostra il runner, ma stanno qui con quelli
// dell'app, pronti per i file di traduzione: il codice nativo non ne ha.

/// Le voci del miniplayer, per chiave.
const miniPlayerLabels = {
  'idle': 'Niente in riproduzione',
  'play': 'Riproduci',
  'pause': 'Pausa',
  'previous': 'Precedente',
  'next': 'Successivo',
  'volume': 'Volume',
  'mute': 'Muto',
  'open': 'Apri megaplayer',
  'quit': 'Esci',
};

/// L'impostazione, con il posto dove sta il miniplayer.
String miniPlayerOption() => defaultTargetPlatform == TargetPlatform.windows
    ? 'Miniplayer nell\'area di notifica'
    : 'Miniplayer nella barra dei menu';

/// Che cosa cambia con il miniplayer acceso.
const miniPlayerDetail = 'Il brano che suona, play e pausa, il volume e la finestra da riaprire. '
    'Chiudendo la finestra la musica continua: per uscire c\'è «Esci» nel miniplayer.';

/// Il miniplayer nel menù di sistema: l'icona nella barra dei menu di macOS,
/// che apre un pannello, o nell'area di notifica di Windows, con il suo menu.
/// Ha titolo e autore del brano, play e pausa, precedente e successivo, il
/// volume e il comando per riaprire la finestra.
///
/// Lo disegna il runner (canale "megaplayer/tray"): da qui gli arriva lo
/// stato, solo quando cambia, e da lì tornano i comandi. Finché c'è,
/// chiudere la finestra la nasconde e la musica continua.
class MiniPlayer {
  MiniPlayer(this.controller) {
    _channel.setMethodCallHandler(_handle);
    controller.addListener(_update);
    _update();
  }

  /// macOS e Windows; non la build web.
  static bool get supported =>
      !kIsWeb &&
      (defaultTargetPlatform == TargetPlatform.macOS || defaultTargetPlatform == TargetPlatform.windows);

  static const _channel = MethodChannel('megaplayer/tray');

  final PlayerController controller;
  Map<String, Object?>? _sent;

  // Il controller avvisa circa 30 volte al secondo: al runner va solo quello
  // che cambia.
  void _update() {
    final state = miniPlayerState(controller);
    if (mapEquals(state, _sent)) return;
    _sent = state;
    _channel
        .invokeMethod<void>('update', state)
        .catchError((_) {}, test: (error) => error is MissingPluginException);
  }

  Future<void> _handle(MethodCall call) async {
    switch (call.method) {
      case 'togglePause':
        controller.togglePause();
      case 'previous':
        controller.previous();
      case 'next':
        controller.next();
      case 'setVolume':
        final value = call.arguments;
        if (value is num) controller.setVolume(value.toDouble());
    }
  }

  void dispose() {
    controller.removeListener(_update);
    _channel.setMethodCallHandler(null);
  }
}

/// Quello che il miniplayer mostra; da spento, solo che è spento. Sotto il
/// titolo l'autore, o il formato se non si sa.
Map<String, Object?> miniPlayerState(PlayerController controller) {
  if (!controller.miniPlayer) return const {'visible': false};
  final track = controller.currentTrack;
  final details = controller.details;
  final index = controller.currentIndex;
  final title = details?.title ?? '';
  final author = details?.author ?? '';
  return {
    'visible': true,
    'title': track == null ? '' : (title.isNotEmpty ? title : track.fileName),
    'subtitle': track == null || details == null ? '' : (author.isNotEmpty ? author : details.formatLabel),
    'playing': controller.status.isPlaying,
    'canPlay': controller.tracks.isNotEmpty,
    'canPrevious': index != null,
    'canNext': index != null && index + 1 < controller.tracks.length,
    'volume': controller.volume,
    'skin': controller.skin.name,
    'labels': miniPlayerLabels,
  };
}
