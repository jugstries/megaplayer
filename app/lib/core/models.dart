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

/// Un brano della coda o di una playlist: un file indicato dall'utente o un
/// file dentro un archivio ("Stormlord.lha > Stormlord/jt.title"), intero o
/// un suo sottobrano.
class Track {
  const Track({required this.ref, required this.name, this.subsong});

  /// Da mp_engine_collect.
  factory Track.fromJson(Map<String, dynamic> json) =>
      Track(ref: json['ref'] as String, name: json['name'] as String);

  /// Da mp_library_playlist_entries.
  factory Track.fromEntryJson(Map<String, dynamic> json) =>
      Track(ref: json['ref'] as String, name: json['name'] as String, subsong: json['subsong'] as int?);

  /// Come ritrovarlo: il percorso, o "ARCHIVIO/percorso/interno".
  final String ref;

  /// Come mostrarlo: il percorso, o "ARCHIVIO > percorso/interno".
  final String name;

  /// Il sottobrano scelto (base 0); null per il file intero, che suona
  /// tutti i sottobrani in fila.
  final int? subsong;

  bool get fromArchive => name.contains(' > ');

  Track withSubsong(int? value) => Track(ref: ref, name: name, subsong: value);

  /// La stessa voce, per le regole delle playlist: lo stesso brano entra più
  /// volte solo con sottobrani diversi.
  bool sameEntry(Track other) => ref == other.ref && subsong == other.subsong;

  /// Il percorso del file, dentro l'archivio se viene da lì.
  String get _innerPath => fromArchive ? name.split(' > ').last : name;

  String get fileName => _innerPath.split(pathSeparators).last;

  /// Archivio e cartella che contengono il file, e il sottobrano se è
  /// scelto, per la seconda riga.
  String get location {
    final parts = _innerPath.split(pathSeparators)..removeLast();
    final String place;
    if (fromArchive) {
      final archive = name.split(' > ').first.split(pathSeparators).last;
      place = [archive, ...parts].join(' · ');
    } else {
      place = parts.isEmpty ? '' : parts.last;
    }
    if (subsong == null) return place;
    final label = 'sottobrano ${subsong! + 1}';
    return place.isEmpty ? label : '$place · $label';
  }
}

/// I separatori dei percorsi: / ovunque, e \ su Windows.
final pathSeparators = RegExp(r'[/\\]');

/// Una playlist salvata.
class PlaylistInfo {
  const PlaylistInfo({required this.id, required this.name, required this.count});

  factory PlaylistInfo.fromJson(Map<String, dynamic> json) =>
      PlaylistInfo(id: json['id'] as int, name: json['name'] as String, count: json['count'] as int);

  final int id;
  final String name;
  final int count;
}

/// Una ricerca nella libreria: le parole cercate, i filtri e la cartella.
class LibraryQuery {
  const LibraryQuery({
    this.text = '',
    this.platforms = const {},
    this.formats = const {},
    this.folder = '',
    this.subfolders = true,
  });

  final String text;
  final Set<String> platforms; // vuoto: tutte
  final Set<String> formats; // vuoto: tutti

  /// La cartella rispetto alla radice ("" per tutta la libreria), con le
  /// sottocartelle se [subfolders].
  final String folder;
  final bool subfolders;

  /// C'è qualcosa da cercare, oltre alla cartella.
  bool get filtered => text.trim().isNotEmpty || platforms.isNotEmpty || formats.isNotEmpty;

  LibraryQuery copyWith({
    String? text,
    Set<String>? platforms,
    Set<String>? formats,
    String? folder,
    bool? subfolders,
  }) =>
      LibraryQuery(
        text: text ?? this.text,
        platforms: platforms ?? this.platforms,
        formats: formats ?? this.formats,
        folder: folder ?? this.folder,
        subfolders: subfolders ?? this.subfolders,
      );
}

/// Un brano della libreria (mp_library_search, mp_library_track).
class LibraryItem {
  const LibraryItem({
    required this.ref,
    required this.file,
    required this.entry,
    required this.name,
    required this.folder,
    required this.backend,
    required this.format,
    required this.player,
    required this.platform,
    required this.platformName,
    required this.title,
    required this.author,
    required this.subsongs,
    required this.startSubsong,
    required this.duration,
    this.durations = const [],
  });

  factory LibraryItem.fromJson(Map<String, dynamic> json) => LibraryItem(
        ref: json['ref'] as String,
        file: json['file'] as String,
        entry: json['entry'] as String,
        name: json['name'] as String,
        folder: json['folder'] as String,
        backend: json['backend'] as String,
        format: json['format'] as String,
        player: json['player'] as String,
        platform: json['platform'] as String,
        platformName: json['platformName'] as String,
        title: json['title'] as String,
        author: json['author'] as String,
        subsongs: json['subsongs'] as int,
        startSubsong: json['startSubsong'] as int,
        duration: (json['duration'] as num?)?.toDouble(),
        durations: [for (final value in json['durations'] as List? ?? const []) (value as num?)?.toDouble()],
      );

  final String ref;
  final String file; // il file su disco, l'archivio se il brano è dentro
  final String entry; // il percorso nell'archivio, vuoto per un file normale
  final String name; // il nome del file del brano
  final String folder; // rispetto alla radice, archivio compreso
  final String backend;
  final String format;
  final String player;
  final String platform;
  final String platformName;
  final String title;
  final String author;

  /// 0 se non si sa senza suonarlo (UADE).
  final int subsongs;
  final int startSubsong;
  final double? duration; // del sottobrano iniziale

  /// La durata di ogni sottobrano, solo da mp_library_track.
  final List<double?> durations;

  String get displayTitle => title.isNotEmpty ? title : name;

  /// Il formato da mostrare: quello dichiarato, altrimenti il replayer.
  String get formatLabel => format.isNotEmpty ? format : player;

  /// La voce di coda o playlist: il file intero, o un suo sottobrano.
  Track toTrack({int? subsong}) => Track(ref: ref, name: entry.isEmpty ? file : '$file > $entry', subsong: subsong);
}

class LibraryPage {
  const LibraryPage({required this.total, required this.items});

  factory LibraryPage.fromJson(Map<String, dynamic> json) => LibraryPage(
        total: json['total'] as int,
        items: [for (final item in json['tracks'] as List) LibraryItem.fromJson(item as Map<String, dynamic>)],
      );

  static const empty = LibraryPage(total: 0, items: []);

  final int total; // tutti i brani della ricerca, non solo questa pagina
  final List<LibraryItem> items;
}

/// Un valore di un filtro (piattaforma, formato) o una cartella, con quanti
/// brani ha.
class LibraryFacet {
  const LibraryFacet({required this.id, required this.name, required this.count});

  final String id;
  final String name;
  final int count;
}

class LibraryFacets {
  const LibraryFacets({required this.platforms, required this.formats});

  factory LibraryFacets.fromJson(Map<String, dynamic> json) => LibraryFacets(
        platforms: [
          for (final item in (json['platforms'] as List).cast<Map<String, dynamic>>())
            LibraryFacet(id: item['id'] as String, name: item['name'] as String, count: item['count'] as int),
        ],
        formats: [
          for (final item in (json['formats'] as List).cast<Map<String, dynamic>>())
            LibraryFacet(id: item['name'] as String, name: item['name'] as String, count: item['count'] as int),
        ],
      );

  static const empty = LibraryFacets(platforms: [], formats: []);

  final List<LibraryFacet> platforms;
  final List<LibraryFacet> formats;
}

/// A che punto è la scansione della cartella della libreria.
class ScanStatus {
  const ScanStatus({
    this.running = false,
    this.walking = false,
    this.filesFound = 0,
    this.filesChecked = 0,
    this.filesRead = 0,
    this.tracksFound = 0,
    this.filesRemoved = 0,
    this.error,
  });

  static const idle = ScanStatus();

  final bool running;
  final bool walking; // ancora a contare i file
  final int filesFound;
  final int filesChecked;
  final int filesRead; // nuovi o cambiati
  final int tracksFound; // nei file letti
  final int filesRemoved;
  final String? error; // se l'ultima scansione è fallita
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
    this.channels = const [],
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
        channels: (json['channels'] as List? ?? const []).cast<String>(),
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

  /// I canali che si possono zittire (voci del chip, canali del tracker o
  /// del MIDI), con il loro nome; "" se non ne hanno uno. Vuoto per i file
  /// audio.
  final List<String> channels;

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

/// A che punto è l'esportazione di un brano.
class ExportStatus {
  const ExportStatus({
    this.running = false,
    this.failed = false,
    this.cancelled = false,
    this.seconds = 0,
    this.total,
    this.error = '',
  });

  final bool running;
  final bool failed;
  final bool cancelled;

  /// Secondi scritti, su [total] se si sa.
  final double seconds;
  final double? total;
  final String error;
}

/// Un modulo Roland emulato da 88emu, per i MIDI.
class RolandDevice {
  const RolandDevice({required this.name, required this.la, required this.available});

  factory RolandDevice.fromJson(Map<String, dynamic> json) => RolandDevice(
        name: json['name'] as String,
        la: json['family'] == 'la',
        available: json['available'] as bool,
      );

  /// Come lo chiama 88emu, es. "SC-55mkII".
  final String name;

  /// Famiglia MT-32 (sintesi LA); altrimenti un Sound Canvas (GS).
  final bool la;

  /// Se le sue ROM ci sono.
  final bool available;
}

class CoreException implements Exception {
  const CoreException(this.message);

  final String message;

  @override
  String toString() => message;
}
