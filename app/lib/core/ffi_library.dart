import 'dart:convert';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'ffi_core.dart';
import 'library_store.dart';
import 'models.dart';

typedef _Library = Pointer<Void>;

/// Binding alle funzioni mp_library_* di libmegaplayer
/// (src/capi/megaplayer.h): playlist, coda e impostazioni.
class FfiLibraryStore implements LibraryStore {
  FfiLibraryStore._(DynamicLibrary lib, this._library)
      : _close = lib.lookupFunction<Void Function(_Library), void Function(_Library)>('mp_library_close'),
        _lastError = lib.lookupFunction<Pointer<Utf8> Function(_Library), Pointer<Utf8> Function(_Library)>(
            'mp_library_last_error'),
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
        _setting = lib.lookupFunction<Pointer<Utf8> Function(_Library, Pointer<Utf8>),
            Pointer<Utf8> Function(_Library, Pointer<Utf8>)>('mp_library_setting'),
        _setSetting = lib.lookupFunction<Int32 Function(_Library, Pointer<Utf8>, Pointer<Utf8>),
            int Function(_Library, Pointer<Utf8>, Pointer<Utf8>)>('mp_library_set_setting');

  /// Apre il database nella cartella dei dati dell'utente, o in [path].
  /// Lancia [CoreException].
  factory FfiLibraryStore.open({String? path}) {
    final lib = DynamicLibrary.open(coreLibraryPath());
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
  final Pointer<Utf8> Function(_Library) _playlists;
  final int Function(_Library, Pointer<Utf8>) _create;
  final int Function(_Library, int, Pointer<Utf8>) _rename;
  final int Function(_Library, int) _delete;
  final int Function(_Library) _queue;
  final Pointer<Utf8> Function(_Library, int) _entries;
  final int Function(_Library, int, Pointer<Utf8>) _setEntries;
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
  String setting(String key) => _withString(key, (native) => _setting(_library, native)).toDartString();

  @override
  void setSetting(String key, String value) => _check(
      _withString(key, (nativeKey) => _withString(value, (nativeValue) => _setSetting(_library, nativeKey, nativeValue))));

  @override
  void dispose() => _close(_library);
}
