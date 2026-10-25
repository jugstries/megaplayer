import 'package:flutter/material.dart';

import 'app.dart';
import 'core/ffi_core.dart';
import 'core/ffi_library.dart';
import 'core/library_store.dart';
import 'core/models.dart';
import 'player/dock_icon.dart';
import 'player/player_controller.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  try {
    // Senza il database l'app suona lo stesso, ma non ricorda niente.
    LibraryStore? store;
    try {
      store = FfiLibraryStore.open();
    } on CoreException catch (e) {
      debugPrint('megaplayer: $e');
    }
    final controller = PlayerController(FfiPlayerCore.open(), store: store)
      ..restore()
      ..start();
    followSkinInDock(controller);
    runApp(MegaplayerApp(controller: controller));
    controller.openInitialFiles();
  } on Object catch (e) {
    // Libreria del core mancante o dispositivo audio non disponibile.
    runApp(StartupErrorApp(message: e is CoreException ? e.message : '$e'));
  }
}
