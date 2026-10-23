import 'dart:math' as math;
import 'dart:typed_data';

import 'package:megaplayer/core/models.dart';
import 'package:megaplayer/core/player_core.dart';

/// Core finto con i brani di Stormlord.lha, per i test senza audio.
class FakeCore implements PlayerCore {
  static const archive = '/Musica/Moduli/Stormlord.lha';
  static const files = [
    'Stormlord/Custom_Version/cust.title',
    'Stormlord/jt.ingame',
    'Stormlord/jt.title',
    'Stormlord/MON_Old_Version/mon.ingame',
    'Stormlord/MON_Old_Version/mon.title',
    'Stormlord/Unused/jt.ingame',
  ];

  String? playing;
  PlaybackState state = PlaybackState.idle;
  double position = 0;
  int subsong = 0;
  final List<String> calls = [];

  bool get _isTitle => playing?.endsWith('jt.title') ?? false;
  int get _subsongs => (playing?.endsWith('ingame') ?? false) ? 5 : 1;

  @override
  List<Backend> backends() => const [
        Backend(name: 'openmpt', description: 'libopenmpt'),
        Backend(name: 'uade', description: 'UADE'),
      ];

  @override
  List<Track> collect(String path) {
    if (path != archive) throw CoreException('impossibile aprire $path');
    return [
      for (final file in files) Track(path: '/tmp/estratto/$file', name: '$archive > $file', fromArchive: true),
    ];
  }

  @override
  void play(String path) {
    calls.add('play ${path.split('/').last}');
    playing = path;
    state = PlaybackState.playing;
    position = 0;
    subsong = 0;
  }

  @override
  void stop() {
    playing = null;
    state = PlaybackState.idle;
  }

  @override
  void setPaused(bool paused) => state = paused ? PlaybackState.paused : PlaybackState.playing;

  @override
  void seek(double seconds) {
    calls.add('seek ${seconds.toStringAsFixed(0)}');
    position = seconds;
  }

  @override
  void selectSubsong(int index) {
    calls.add('subsong $index');
    subsong = index;
    position = 0;
  }

  @override
  void setVolume(double volume) {}

  @override
  CoreStatus status() => CoreStatus(
        state: state,
        position: position,
        duration: _isTitle ? 184.48 : null,
        subsong: subsong,
        subsongCount: playing == null ? 0 : _subsongs,
      );

  @override
  TrackDetails details() => TrackDetails(
        file: playing!.split('/').last,
        packers: const [],
        backend: 'uade',
        format: '',
        player: playing!.contains('cust') ? 'custom' : 'Jeroen Tel',
        title: '',
        author: '',
        subsongCount: _subsongs,
        duration: _isTitle ? 184.48 : null,
      );

  @override
  Float32List scope(int frames) {
    final samples = Float32List(frames * 2);
    for (var i = 0; i < frames; i++) {
      final t = i / frames;
      final square = math.sin(2 * math.pi * 3 * t) >= 0 ? 0.25 : -0.25;
      samples[i * 2] = 0.45 * math.sin(2 * math.pi * 7 * t) + square;
      samples[i * 2 + 1] = 0.35 * math.sin(2 * math.pi * 11 * t + 1) + square * 0.6;
    }
    return samples;
  }

  @override
  void dispose() {}
}
