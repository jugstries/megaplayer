import 'package:flutter/services.dart';

import 'player_controller.dart';

const _icon = MethodChannel('megaplayer/icon');

/// L'icona nel Dock segue la skin finché l'app è aperta (canale
/// "megaplayer/icon", per ora solo su macOS); il bundle porta quella
/// moderna. Dove il canale non c'è resta l'icona dell'app.
void followSkinInDock(PlayerController controller) {
  Skin? shown;
  void update() {
    if (controller.skin == shown) return;
    shown = controller.skin;
    _icon
        .invokeMethod<void>('setSkin', controller.skin.name)
        .catchError((_) {}, test: (error) => error is MissingPluginException);
  }

  controller.addListener(update);
  update();
}
