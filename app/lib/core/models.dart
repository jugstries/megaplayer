// Dati scambiati con il core C++ (vedi src/capi/megaplayer.h).

enum PlaybackState { idle, playing, paused, ended }

class CoreStatus {
  const CoreStatus({
    required this.state,
    required this.position,
    required this.duration,
    required this.subsong,
    required this.subsongCount,
  });

  static const idle = CoreStatus(
    state: PlaybackState.idle,
    position: 0,
    duration: null,
    subsong: 0,
    subsongCount: 0,
  );

  final PlaybackState state;
  final double position; // secondi
  final double? duration; // secondi, null se sconosciuta
  final int subsong; // base 0
  final int subsongCount;

  bool get isPlaying => state == PlaybackState.playing;
}

/// Un brano della coda: un file indicato dall'utente o estratto da un
/// archivio ("Stormlord.lha > Stormlord/jt.title").
class Track {
  const Track({required this.path, required this.name, required this.fromArchive});

  factory Track.fromJson(Map<String, dynamic> json) => Track(
        path: json['path'] as String,
        name: json['name'] as String,
        fromArchive: json['archive'] as bool,
      );

  final String path;
  final String name;
  final bool fromArchive;

  /// Il percorso del file, dentro l'archivio se viene da lì.
  String get _innerPath => fromArchive ? name.split(' > ').last : name;

  String get fileName => _innerPath.split('/').last;

  /// Archivio e cartella che contengono il file, per la seconda riga.
  String get location {
    final parts = _innerPath.split('/')..removeLast();
    if (fromArchive) {
      final archive = name.split(' > ').first.split('/').last;
      return [archive, ...parts].join(' · ');
    }
    return parts.isEmpty ? '' : parts.last;
  }
}

/// Informazioni sul brano corrente, da backend e replayer.
class TrackDetails {
  const TrackDetails({
    required this.file,
    required this.packers,
    required this.backend,
    required this.format,
    required this.player,
    required this.title,
    required this.author,
    required this.subsongCount,
    required this.duration,
  });

  factory TrackDetails.fromJson(Map<String, dynamic> json) => TrackDetails(
        file: json['file'] as String,
        packers: (json['packers'] as List).cast<String>(),
        backend: json['backend'] as String,
        format: json['format'] as String,
        player: json['player'] as String,
        title: json['title'] as String,
        author: json['author'] as String,
        subsongCount: json['subsongCount'] as int,
        duration: (json['duration'] as num?)?.toDouble(),
      );

  final String file;
  final List<String> packers;
  final String backend;
  final String format; // può essere vuoto: alcuni replayer non lo dicono
  final String player;
  final String title;
  final String author;
  final int subsongCount;
  final double? duration;

  /// Il formato da mostrare: quello dichiarato, altrimenti il replayer.
  String get formatLabel => format.isNotEmpty ? format : player;
}

class Backend {
  const Backend({required this.name, required this.description});

  factory Backend.fromJson(Map<String, dynamic> json) =>
      Backend(name: json['name'] as String, description: json['description'] as String);

  final String name;
  final String description;
}

class CoreException implements Exception {
  const CoreException(this.message);

  final String message;

  @override
  String toString() => message;
}
