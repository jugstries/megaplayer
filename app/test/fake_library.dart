import 'package:megaplayer/core/library_store.dart';
import 'package:megaplayer/core/models.dart';

/// Il database della libreria in memoria, con le stesse regole del core: la
/// coda è una playlist nascosta e i doppioni si scartano.
class FakeLibraryStore implements LibraryStore {
  final Map<int, String> _names = {};
  final Map<int, List<Track>> _entries = {};
  final Map<String, String> settings = {};
  int? _queue;
  int _nextId = 1;
  int writes = 0;

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
