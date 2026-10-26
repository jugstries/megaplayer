import 'dart:convert';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'ffi_core.dart';
import 'library_store.dart';
import 'models.dart';

typedef _Library = Pointer<Void>;
typedef _Text = Pointer<Utf8>;
typedef _SearchNative = _Text Function(_Library, _Text, _Text, _Text, _Text, Int32, Int32, Int32);
typedef _Search = _Text Function(_Library, _Text, _Text, _Text, _Text, int, int, int);
typedef _Counts = _Text Function(_Library, _Text, _Text, _Text, _Text);

final class _ScanStatus extends Struct {
  @Int32()
  external int running;
  @Int32()
  external int walking;
  @Int32()
  external int filesFound;
  @Int32()
  external int filesChecked;
  @Int32()
  external int filesRead;
  @Int32()
  external int tracksFound;
  @Int32()
  external int filesRemoved;
  @Int32()
  external int failed;
}

/// Binding alle funzioni mp_library_* di libmegaplayer
/// (src/capi/megaplayer.h): l'indice della libreria, playlist, coda e
/// impostazioni.
class FfiLibraryStore implements LibraryStore {
  FfiLibraryStore._(DynamicLibrary lib, this._library)
      : _close = lib.lookupFunction<Void Function(_Library), void Function(_Library)>('mp_library_close'),
        _lastError = lib.lookupFunction<Pointer<Utf8> Function(_Library), Pointer<Utf8> Function(_Library)>(
            'mp_library_last_error'),
        _root = lib.lookupFunction<_Text Function(_Library), _Text Function(_Library)>('mp_library_root'),
        _setRoot = lib.lookupFunction<Int32 Function(_Library, _Text), int Function(_Library, _Text)>(
            'mp_library_set_root'),
        _scan = lib.lookupFunction<Int32 Function(_Library), int Function(_Library)>('mp_library_scan'),
        _cancelScan = lib.lookupFunction<Void Function(_Library), void Function(_Library)>('mp_library_cancel_scan'),
        _scanStatus = lib.lookupFunction<Void Function(_Library, Pointer<_ScanStatus>),
            void Function(_Library, Pointer<_ScanStatus>)>('mp_library_scan_status'),
        _search = lib.lookupFunction<_SearchNative, _Search>('mp_library_search'),
        _facets = lib.lookupFunction<_Counts, _Counts>('mp_library_facets'),
        _folders = lib.lookupFunction<_Counts, _Counts>('mp_library_folders'),
        _track = lib.lookupFunction<_Text Function(_Library, _Text), _Text Function(_Library, _Text)>(
            'mp_library_track'),
        _status = calloc<_ScanStatus>(),
        _playlists = lib.lookupFunction<Pointer<Utf8> Function(_Library), Pointer<Utf8> Function(_Library)>(
            'mp_library_playlists'),
        _create = lib.lookupFunction<Int64 Function(_Library, Pointer<Utf8>), int Function(_Library, Pointer<Utf8>)>(
            'mp_library_playlist_create'),
        _rename = lib.lookupFunction<Int32 Function(_Library, Int64, Pointer<Utf8>),
            int Function(_Library, int, Pointer<Utf8>)>('mp_library_playlist_rename'),
        _delete = lib.lookupFunction<Int32 Function(_Library, Int64), int Function(_Library, int)>(
            'mp_library_playlist_delete'),
        _queue = lib.lookupFunction<Int64 Function(_Library), int Function(_Library)>('mp_library_queue'),
        _entries = lib.lookupFunction<Pointer<Utf8> Function(_Library, Int64), Pointer<Utf8> Function(_Library, int)>(
            'mp_library_playlist_entries'),
        _setEntries = lib.lookupFunction<Int32 Function(_Library, Int64, Pointer<Utf8>),
            int Function(_Library, int, Pointer<Utf8>)>('mp_library_playlist_set_entries'),
        _import = lib.lookupFunction<_Text Function(_Library, _Text), _Text Function(_Library, _Text)>(
            'mp_library_playlist_import'),
        _export = lib.lookupFunction<Int32 Function(_Library, Int64, _Text), int Function(_Library, int, _Text)>(
            'mp_library_playlist_export'),
        _setting = lib.lookupFunction<Pointer<Utf8> Function(_Library, Pointer<Utf8>),
            Pointer<Utf8> Function(_Library, Pointer<Utf8>)>('mp_library_setting'),
        _setSetting = lib.lookupFunction<Int32 Function(_Library, Pointer<Utf8>, Pointer<Utf8>),
            int Function(_Library, Pointer<Utf8>, Pointer<Utf8>)>('mp_library_set_setting');

  /// Apre il database nella cartella dei dati dell'utente, o in [path].
  /// Lancia [CoreException].
  factory FfiLibraryStore.open({String? path}) {
    final lib = loadCoreLibrary();
    final open =
        lib.lookupFunction<_Library Function(Pointer<Utf8>), _Library Function(Pointer<Utf8>)>('mp_library_open');
    final native = path?.toNativeUtf8() ?? nullptr;
    final library = open(native.cast());
    if (native != nullptr) calloc.free(native);
    if (library == nullptr) throw const CoreException('impossibile aprire il database della libreria');
    return FfiLibraryStore._(lib, library);
  }

  final _Library _library;
  final void Function(_Library) _close;
  final Pointer<Utf8> Function(_Library) _lastError;
  final _Text Function(_Library) _root;
  final int Function(_Library, _Text) _setRoot;
  final int Function(_Library) _scan;
  final void Function(_Library) _cancelScan;
  final void Function(_Library, Pointer<_ScanStatus>) _scanStatus;
  final _Search _search;
  final _Counts _facets;
  final _Counts _folders;
  final _Text Function(_Library, _Text) _track;
  final Pointer<_ScanStatus> _status;
  final Pointer<Utf8> Function(_Library) _playlists;
  final int Function(_Library, Pointer<Utf8>) _create;
  final int Function(_Library, int, Pointer<Utf8>) _rename;
  final int Function(_Library, int) _delete;
  final int Function(_Library) _queue;
  final Pointer<Utf8> Function(_Library, int) _entries;
  final int Function(_Library, int, Pointer<Utf8>) _setEntries;
  final _Text Function(_Library, _Text) _import;
  final int Function(_Library, int, _Text) _export;
  final Pointer<Utf8> Function(_Library, Pointer<Utf8>) _setting;
  final int Function(_Library, Pointer<Utf8>, Pointer<Utf8>) _setSetting;

  CoreException _error() => CoreException(_lastError(_library).toDartString());

  T _withString<T>(String text, T Function(Pointer<Utf8>) body) {
    final native = text.toNativeUtf8();
    try {
      return body(native);
    } finally {
      calloc.free(native);
    }
  }

  void _check(int result) {
    if (result < 0) throw _error();
  }

  List<dynamic> _json(Pointer<Utf8> text) {
    if (text == nullptr) throw _error();
    return jsonDecode(text.toDartString()) as List;
  }

  /// I campi di una ricerca come stringhe C, liberate alla fine.
  T _withQuery<T>(LibraryQuery query, T Function(_Text text, _Text platforms, _Text formats, _Text folder) body) =>
      _withString(query.text, (text) {
        return _withString(query.platforms.join('\n'), (platforms) {
          return _withString(query.formats.join('\n'), (formats) {
            return _withString(query.folder, (folder) => body(text, platforms, formats, folder));
          });
        });
      });

  @override
  String root() => _root(_library).toDartString();

  @override
  void setRoot(String path) => _check(_withString(path, (native) => _setRoot(_library, native)));

  @override
  void startScan() => _check(_scan(_library));

  @override
  void cancelScan() => _cancelScan(_library);

  @override
  ScanStatus scanStatus() {
    _scanStatus(_library, _status);
    final status = _status.ref;
    return ScanStatus(
      running: status.running != 0,
      walking: status.walking != 0,
      filesFound: status.filesFound,
      filesChecked: status.filesChecked,
      filesRead: status.filesRead,
      tracksFound: status.tracksFound,
      filesRemoved: status.filesRemoved,
      error: status.failed != 0 ? _lastError(_library).toDartString() : null,
    );
  }

  @override
  LibraryPage search(LibraryQuery query, {int offset = 0, int limit = 200}) {
    final json = _withQuery(query, (text, platforms, formats, folder) {
      return _search(_library, text, platforms, formats, folder, query.subfolders ? 1 : 0, offset, limit);
    });
    return LibraryPage.fromJson(jsonDecode(json.toDartString()) as Map<String, dynamic>);
  }

  @override
  LibraryFacets facets(LibraryQuery query) {
    final json = _withQuery(query, (text, platforms, formats, folder) {
      return _facets(_library, text, platforms, formats, folder);
    });
    return LibraryFacets.fromJson(jsonDecode(json.toDartString()) as Map<String, dynamic>);
  }

  @override
  List<LibraryFacet> folders(LibraryQuery query) {
    final json = _withQuery(query, (text, platforms, formats, folder) {
      return _folders(_library, text, platforms, formats, folder);
    });
    return [
      for (final item in (jsonDecode(json.toDartString()) as List).cast<Map<String, dynamic>>())
        LibraryFacet(id: item['name'] as String, name: item['name'] as String, count: item['count'] as int),
    ];
  }

  @override
  LibraryItem? track(String ref) {
    final json = _withString(ref, (native) => _track(_library, native)).toDartString();
    return json.isEmpty ? null : LibraryItem.fromJson(jsonDecode(json) as Map<String, dynamic>);
  }

  @override
  List<PlaylistInfo> playlists() =>
      _json(_playlists(_library)).map((item) => PlaylistInfo.fromJson(item as Map<String, dynamic>)).toList();

  @override
  int createPlaylist(String name) {
    final id = _withString(name, (native) => _create(_library, native));
    _check(id);
    return id;
  }

  @override
  void renamePlaylist(int id, String name) => _check(_withString(name, (native) => _rename(_library, id, native)));

  @override
  void deletePlaylist(int id) => _check(_delete(_library, id));

  @override
  int queueId() {
    final id = _queue(_library);
    _check(id);
    return id;
  }

  @override
  List<Track> entries(int id) =>
      _json(_entries(_library, id)).map((item) => Track.fromEntryJson(item as Map<String, dynamic>)).toList();

  @override
  void setEntries(int id, List<Track> entries) {
    // Una voce per riga, "SOTTOBRANO\tREF\tNOME": tabulazioni e a capo nei
    // nomi non possono esserci.
    String clean(String text) => text.replaceAll(RegExp('[\t\n]'), ' ');
    final lines = [for (final entry in entries) '${entry.subsong ?? ''}\t${clean(entry.ref)}\t${clean(entry.name)}'];
    _check(_withString(lines.join('\n'), (native) => _setEntries(_library, id, native)));
  }

  @override
  PlaylistImport importPlaylist(String path) {
    final json = _withString(path, (native) => _import(_library, native));
    if (json == nullptr) throw _error();
    return PlaylistImport.fromJson(jsonDecode(json.toDartString()) as Map<String, dynamic>);
  }

  @override
  void exportPlaylist(int id, String path) => _check(_withString(path, (native) => _export(_library, id, native)));

  @override
  String setting(String key) => _withString(key, (native) => _setting(_library, native)).toDartString();

  @override
  void setSetting(String key, String value) => _check(
      _withString(key, (nativeKey) => _withString(value, (nativeValue) => _setSetting(_library, nativeKey, nativeValue))));

  @override
  void dispose() {
    _close(_library);
    calloc.free(_status);
  }
}
