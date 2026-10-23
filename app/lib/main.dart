import 'package:flutter/material.dart';

import 'app.dart';
import 'core/ffi_core.dart';
import 'core/models.dart';
import 'player/player_controller.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  try {
    final controller = PlayerController(FfiPlayerCore.open())..start();
    runApp(MegaplayerApp(controller: controller));
    controller.openInitialFiles();
  } on Object catch (e) {
    // Libreria del core mancante o dispositivo audio non disponibile.
    runApp(StartupErrorApp(message: e is CoreException ? e.message : '$e'));
  }
}
