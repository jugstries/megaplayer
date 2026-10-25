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
        Backend(name: 'fluidsynth', description: 'FluidSynth'),
      ];

  @override
  List<Track> collect(String path) {
    if (path != archive) throw CoreException('impossibile aprire $path');
    return [
      for (final file in files) Track(ref: '$archive/$file', name: '$archive > $file'),
    ];
  }

  @override
  void play(String ref) {
    calls.add('play ${ref.split('/').last}');
    playing = ref;
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
  void setMegaSound(double amount) => calls.add('megasound ${(amount * 100).round()}');

  @override
  void setChannelMuted(int channel, bool muted) => calls.add('${muted ? 'mute' : 'unmute'} $channel');

  /// Le cartelle di ROM aggiunte e il modulo scelto; i moduli con le ROM
  /// sono quelli di [romsIn], se una delle cartelle è quella.
  final List<String> romDirectories = [];
  String rolandDevice = '';
  String romsIn = '/Roms/Roland';

  /// Il SoundFont in uso; all'avvio quello "incluso".
  String currentSoundFont = '/App/soundfonts/GeneralUser-GS.sf2';

  @override
  void setSoundFont(String path) => currentSoundFont = path;

  @override
  String soundFont() => currentSoundFont;

  @override
  void addRolandRomDirectory(String path) => romDirectories.add(path);

  @override
  void setRolandDevice(String name) => rolandDevice = name;

  @override
  List<RolandDevice> rolandDevices() {
    final found = romDirectories.contains(romsIn);
    return [
      RolandDevice(name: 'SC-55mkII', la: false, available: found),
      RolandDevice(name: 'SC-88Pro', la: false, available: found),
      const RolandDevice(name: 'SC-8850', la: false, available: false),
      RolandDevice(name: 'MT-32 1.x (experimental)', la: true, available: found),
    ];
  }

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
        channels: const ['Voce 1 · sinistra', 'Voce 2 · destra', 'Voce 3 · destra', 'Voce 4 · sinistra'],
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
