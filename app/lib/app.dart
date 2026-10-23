import 'package:flutter/material.dart';

import 'player/player_controller.dart';
import 'skins/modern/modern_skin.dart';
import 'skins/retro/retro_skin.dart';

class MegaplayerApp extends StatelessWidget {
  const MegaplayerApp({super.key, required this.controller});

  final PlayerController controller;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'megaplayer',
      debugShowCheckedModeBanner: false,
      home: ListenableBuilder(
        listenable: controller,
        builder: (context, _) => switch (controller.skin) {
          Skin.modern => ModernSkin(controller: controller),
          Skin.retro => RetroSkin(controller: controller),
        },
      ),
    );
  }
}

class StartupErrorApp extends StatelessWidget {
  const StartupErrorApp({super.key, required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        backgroundColor: const Color(0xFF0C0B10),
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Text(
              'megaplayer non riesce a partire:\n$message',
              textAlign: TextAlign.center,
              style: const TextStyle(color: Color(0xFFF2EFF7), fontSize: 16, height: 1.5),
            ),
          ),
        ),
      ),
    );
  }
}
