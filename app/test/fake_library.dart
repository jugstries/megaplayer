import 'package:megaplayer/core/library_store.dart';
import 'package:megaplayer/core/models.dart';

/// Un brano della libreria finta: [folder] rispetto alla radice.
LibraryItem libraryItem(
  String folder,
  String name, {
  String platform = 'amiga',
  String format = 'ProTracker',
  String title = '',
  int subsongs = 1,
  List<double?> durations = const [],
}) =>
    LibraryItem(
      ref: '/Musica/${folder.isEmpty ? '' : '$folder/'}$name',
      file: '/Musica/${folder.isEmpty ? '' : '$folder/'}$name',
      entry: '',
      name: name,
      folder: folder,
      backend: 'openmpt',
      format: format,
      player: '',
      platform: platform,
      platformName: platform,
      title: title,
      author: '',
      subsongs: subsongs,
      startSubsong: 0,
      duration: durations.isEmpty ? null : durations.first,
      durations: durations,
    );

/// Il database della libreria in memoria, con le stesse regole del core: la
/// coda è una playlist nascosta e i doppioni si scartano. La scansione non
/// legge niente: finisce quando il test chiama [finishScan].
class FakeLibraryStore implements LibraryStore {
  String rootPath = '';
  final List<LibraryItem> items = [];
  ScanStatus status = ScanStatus.idle;
  int scans = 0;
  bool cancelled = false;
  int searches = 0;

  final Map<int, String> _names = {};
  final Map<int, List<Track>> _entries = {};
  final Map<String, String> settings = {};
  int? _queue;
  int _nextId = 1;
  int writes = 0;

  /// La scansione in corso finisce, e nella libreria ci sono [found].
  void finishScan([List<LibraryItem> found = const []]) {
    items
      ..clear()
      ..addAll(found);
    status = ScanStatus(tracksFound: found.length);
  }

  @override
  String root() => rootPath;

  @override
  void setRoot(String path) {
    if (status.running) throw const CoreException('scansione in corso');
    rootPath = path;
    items.clear();
  }

  @override
  void startScan() {
    if (rootPath.isEmpty) throw const CoreException('cartella della libreria non scelta');
    if (status.running) throw const CoreException('scansione già in corso');
    ++scans;
    cancelled = false;
    status = const ScanStatus(running: true, walking: true);
  }

  @override
  void cancelScan() => cancelled = true;

  @override
  ScanStatus scanStatus() => status;

  bool _inFolder(LibraryItem item, String folder, bool subfolders) {
    if (folder.isEmpty) return subfolders || item.folder.isEmpty;
    return item.folder == folder || (subfolders && item.folder.startsWith('$folder/'));
  }

  bool _matches(LibraryItem item, LibraryQuery query, {bool platforms = true, bool formats = true}) {
    final words = query.text.toLowerCase().split(' ').where((word) => word.isNotEmpty);
    final text = '${item.title} ${item.author} ${item.name} ${item.folder}'.toLowerCase();
    return words.every(text.contains) &&
        (!platforms || query.platforms.isEmpty || query.platforms.contains(item.platform)) &&
        (!formats || query.formats.isEmpty || query.formats.contains(item.format));
  }

  @override
  LibraryPage search(LibraryQuery query, {int offset = 0, int limit = 200}) {
    ++searches;
    final found = items.where((item) => _inFolder(item, query.folder, query.subfolders) && _matches(item, query)).toList()
      ..sort((a, b) => '${a.folder}/${a.name}'.compareTo('${b.folder}/${b.name}'));
    return LibraryPage(total: found.length, items: found.skip(offset).take(limit).toList());
  }

  List<LibraryFacet> _count(Iterable<String> values) {
    final counts = <String, int>{};
    for (final value in values) {
      counts[value] = (counts[value] ?? 0) + 1;
    }
    return [for (final MapEntry(:key, :value) in counts.entries) LibraryFacet(id: key, name: key, count: value)];
  }

  @override
  LibraryFacets facets(LibraryQuery query) {
    final scoped = items.where((item) => _inFolder(item, query.folder, true));
    return LibraryFacets(
      platforms: _count(scoped.where((item) => _matches(item, query, platforms: false)).map((item) => item.platform)),
      formats: _count(scoped.where((item) => _matches(item, query, formats: false)).map((item) => item.format)),
    );
  }

  @override
  List<LibraryFacet> folders(LibraryQuery query) {
    final prefix = query.folder.isEmpty ? '' : '${query.folder}/';
    final below = items.where((item) =>
        item.folder.startsWith(prefix) && item.folder.length > prefix.length && _matches(item, query));
    return _count(below.map((item) => item.folder.substring(prefix.length).split('/').first))
      ..sort((a, b) => a.name.compareTo(b.name));
  }

  @override
  LibraryItem? track(String ref) => items.where((item) => item.ref == ref).firstOrNull;

  @override
  List<PlaylistInfo> playlists() => [
        for (final id in _names.keys)
          if (id != _queue) PlaylistInfo(id: id, name: _names[id]!, count: _entries[id]!.length),
      ];

  @override
  int createPlaylist(String name) {
    final id = _nextId++;
    _names[id] = name;
    _entries[id] = [];
    return id;
  }

  void _require(int id) {
    if (!_names.containsKey(id)) throw const CoreException('playlist inesistente');
  }

  @override
  void renamePlaylist(int id, String name) {
    _require(id);
    _names[id] = name;
  }

  @override
  void deletePlaylist(int id) {
    _require(id);
    if (id == _queue) throw const CoreException('playlist inesistente');
    _names.remove(id);
    _entries.remove(id);
  }

  @override
  int queueId() => _queue ??= createPlaylist('');

  @override
  List<Track> entries(int id) {
    _require(id);
    return List.of(_entries[id]!);
  }

  @override
  void setEntries(int id, List<Track> entries) {
    _require(id);
    ++writes;
    final kept = <Track>[];
    for (final entry in entries) {
      if (!kept.any(entry.sameEntry)) kept.add(entry);
    }
    _entries[id] = kept;
  }

  @override
  String setting(String key) => settings[key] ?? '';

  @override
  void setSetting(String key, String value) => settings[key] = value;

  @override
  void dispose() {}
}
