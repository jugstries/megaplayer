import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import '../core/models.dart';
import '../core/player_core.dart';

enum Skin { modern, retro }

/// Stato e comandi del player, condivisi dalle due skin: la coda, il brano
/// corrente e quello che il core riporta a ogni aggiornamento.
class PlayerController extends ChangeNotifier {
  PlayerController(this.core) : backends = core.backends();

  final PlayerCore core;
  final List<Backend> backends;

  final List<Track> tracks = [];
  int? currentIndex;
  CoreStatus status = CoreStatus.idle;
  TrackDetails? details;
  Float32List scope = Float32List(0);
  double volume = 0.8;

  /// La manopola MegaSound, 0..1. Resta dov'è da un brano all'altro.
  double megaSound = 0;

  /// Confronto A/B: la manopola resta dov'è ma si sente l'originale.
  bool megaSoundBypassed = false;
  Skin skin = Skin.modern;
  String? error;

  static const _files = MethodChannel('megaplayer/files');
  static const scopeFrames = 1024;

  Timer? _timer;

  Track? get currentTrack => currentIndex == null ? null : tracks[currentIndex!];

  /// Avvia gli aggiornamenti periodici dello stato (circa 30 al secondo).
  void start() {
    core.setVolume(volume);
    _timer ??= Timer.periodic(const Duration(milliseconds: 33), (_) => refresh());
  }

  /// Legge lo stato dal core; a fine brano passa al successivo.
  void refresh() {
    status = core.status();
    scope = core.scope(scopeFrames);
    if (status.state == PlaybackState.ended) {
      _advance();
    }
    notifyListeners();
  }

  void _advance() {
    final index = currentIndex;
    if (index != null && index + 1 < tracks.length) {
      playIndex(index + 1);
    } else {
      core.stop();
      status = CoreStatus.idle;
    }
  }

  /// La finestra di apertura file di macOS.
  Future<void> openFiles() async {
    final paths = await _files.invokeListMethod<String>('openFiles') ?? const [];
    addPaths(paths);
  }

  /// I file passati da riga di comando all'avvio.
  Future<void> openInitialFiles() async {
    final paths = await _files.invokeListMethod<String>('initialFiles') ?? const [];
    addPaths(paths);
  }

  /// Aggiunge file e archivi alla coda; se non sta suonando nulla parte il
  /// primo brano aggiunto.
  void addPaths(List<String> paths) {
    final firstNew = tracks.length;
    final problems = <String>[];
    for (final path in paths) {
      try {
        tracks.addAll(core.collect(path));
      } on CoreException catch (e) {
        problems.add('${path.split('/').last}: $e');
      }
    }
    error = problems.isEmpty ? null : problems.join('\n');
    if (currentIndex == null && tracks.length > firstNew) {
      playIndex(firstNew);
    } else {
      notifyListeners();
    }
  }

  void playIndex(int index) {
    if (index < 0 || index >= tracks.length) return;
    currentIndex = index;
    try {
      core.play(tracks[index].path);
      details = core.details();
      error = null;
    } on CoreException catch (e) {
      details = null;
      error = '${tracks[index].fileName}: $e';
    }
    status = core.status();
    notifyListeners();
  }

  void togglePause() {
    if (status.state == PlaybackState.idle || status.state == PlaybackState.ended) {
      if (tracks.isNotEmpty) playIndex(currentIndex ?? 0);
      return;
    }
    core.setPaused(status.isPlaying);
    refresh();
  }

  void next() {
    if (currentIndex != null) playIndex(currentIndex! + 1);
  }

  void previous() {
    final index = currentIndex;
    if (index == null) return;
    // Come nei lettori classici: oltre i primi secondi si riparte dall'inizio.
    if (status.position > 3 || index == 0) {
      seekTo(0);
    } else {
      playIndex(index - 1);
    }
  }

  void seekTo(double seconds) {
    core.seek(seconds < 0 ? 0 : seconds);
    refresh();
  }

  void seekBy(double delta) => seekTo(status.position + delta);

  void selectSubsong(int index) {
    if (index < 0 || index >= status.subsongCount) return;
    core.selectSubsong(index);
    refresh();
  }

  void setVolume(double value) {
    volume = value.clamp(0.0, 1.0);
    core.setVolume(volume);
    notifyListeners();
  }

  void setMegaSound(double value) {
    megaSound = value.clamp(0.0, 1.0);
    // Chi gira la manopola vuole sentire l'effetto, non l'originale.
    megaSoundBypassed = false;
    _applyMegaSound();
    notifyListeners();
  }

  void toggleMegaSoundCompare() {
    megaSoundBypassed = !megaSoundBypassed;
    _applyMegaSound();
    notifyListeners();
  }

  void _applyMegaSound() => core.setMegaSound(megaSoundBypassed ? 0 : megaSound);

  void setSkin(Skin value) {
    skin = value;
    notifyListeners();
  }

  @override
  void dispose() {
    _timer?.cancel();
    core.dispose();
    super.dispose();
  }
}

/// "1:12", oppure "--:--" se il tempo non è noto.
String formatTime(double? seconds, {bool padMinutes = false}) {
  if (seconds == null) return '--:--';
  final total = seconds.floor();
  final minutes = (total ~/ 60).toString();
  final rest = (total % 60).toString().padLeft(2, '0');
  return '${padMinutes ? minutes.padLeft(2, '0') : minutes}:$rest';
}
