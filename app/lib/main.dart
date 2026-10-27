import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import 'app.dart';
import 'core/ffi_core.dart';
import 'core/ffi_library.dart';
import 'core/library_store.dart';
import 'core/models.dart';
import 'player/dock_icon.dart';
import 'player/mini_player.dart';
import 'player/player_controller.dart';
import 'update/updater.dart';
import 'update/updates.dart';

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
    late final PlayerController controller;
    final updater = Updater.forThisApp();
    // Prima dell'installer la musica si ferma e il database si chiude.
    final updates = updater == null
        ? null
        : Updates(updater, quit: () async {
            controller.dispose();
            exit(0);
          });
    controller = PlayerController(
      FfiPlayerCore.open(),
      store: store,
      updates: updates,
      miniPlayerSupported: MiniPlayer.supported,
    )
      ..restore()
      ..start();
    followSkinInDock(controller);
    if (MiniPlayer.supported) MiniPlayer(controller);
    runApp(MegaplayerApp(controller: controller));
    controller.openInitialFiles();
    // Qualche secondo dopo l'avvio, per non rallentarlo.
    if (updates != null) Timer(const Duration(seconds: 3), updates.start);
  } on Object catch (e) {
    // Libreria del core mancante o dispositivo audio non disponibile.
    runApp(StartupErrorApp(message: e is CoreException ? e.message : '$e'));
  }
}
