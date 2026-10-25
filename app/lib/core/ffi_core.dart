import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'models.dart';
import 'player_core.dart';

final class _MpStatus extends Struct {
  @Int32()
  external int state;
  @Int32()
  external int subsong;
  @Int32()
  external int subsongCount;
  @Int32()
  external int reserved;
  @Double()
  external double position;
  @Double()
  external double duration;
}

typedef _Engine = Pointer<Void>;

/// La libreria del core: nel bundle dell'app (la copia
/// scripts/embed-core-macos.sh), oppure dove indica MEGAPLAYER_CORE.
String coreLibraryPath() {
  final override = Platform.environment['MEGAPLAYER_CORE'];
  if (override != null) return override;
  final contents = File(Platform.resolvedExecutable).parent.parent;
  return '${contents.path}/Frameworks/libmegaplayer.dylib';
}

/// Binding a libmegaplayer (src/capi/megaplayer.h) via dart:ffi.
class FfiPlayerCore implements PlayerCore {
  FfiPlayerCore._(DynamicLibrary lib, this._engine)
      : _destroy = lib.lookupFunction<Void Function(_Engine), void Function(_Engine)>('mp_engine_destroy'),
        _lastError = lib.lookupFunction<Pointer<Utf8> Function(_Engine), Pointer<Utf8> Function(_Engine)>(
            'mp_engine_last_error'),
        _backends = lib.lookupFunction<Pointer<Utf8> Function(_Engine), Pointer<Utf8> Function(_Engine)>(
            'mp_engine_backends'),
        _collect = lib.lookupFunction<Pointer<Utf8> Function(_Engine, Pointer<Utf8>),
            Pointer<Utf8> Function(_Engine, Pointer<Utf8>)>('mp_engine_collect'),
        _play = lib.lookupFunction<Int32 Function(_Engine, Pointer<Utf8>), int Function(_Engine, Pointer<Utf8>)>(
            'mp_engine_play'),
        _stop = lib.lookupFunction<Void Function(_Engine), void Function(_Engine)>('mp_engine_stop'),
        _setPaused =
            lib.lookupFunction<Void Function(_Engine, Int32), void Function(_Engine, int)>('mp_engine_set_paused'),
        _seek = lib.lookupFunction<Void Function(_Engine, Double), void Function(_Engine, double)>('mp_engine_seek'),
        _selectSubsong = lib.lookupFunction<Void Function(_Engine, Int32), void Function(_Engine, int)>(
            'mp_engine_select_subsong'),
        _setVolume =
            lib.lookupFunction<Void Function(_Engine, Float), void Function(_Engine, double)>('mp_engine_set_volume'),
        _setMegaSound = lib.lookupFunction<Void Function(_Engine, Float), void Function(_Engine, double)>(
            'mp_engine_set_megasound'),
        _status = lib.lookupFunction<Void Function(_Engine, Pointer<_MpStatus>),
            void Function(_Engine, Pointer<_MpStatus>)>('mp_engine_status'),
        _trackInfo = lib.lookupFunction<Pointer<Utf8> Function(_Engine), Pointer<Utf8> Function(_Engine)>(
            'mp_engine_track_info'),
        _scope = lib.lookupFunction<Int32 Function(_Engine, Pointer<Float>, Int32),
            int Function(_Engine, Pointer<Float>, int)>('mp_engine_scope');

  /// Carica la libreria e apre il dispositivo audio. Lancia [CoreException].
  factory FfiPlayerCore.open({int sampleRate = 48000}) {
    final lib = DynamicLibrary.open(coreLibraryPath());
    final create = lib.lookupFunction<_Engine Function(Int32), _Engine Function(int)>('mp_engine_create');
    final engine = create(sampleRate);
    if (engine == nullptr) {
      throw const CoreException('impossibile aprire il dispositivo audio');
    }
    return FfiPlayerCore._(lib, engine);
  }

  static const _scopeCapacity = 4096;

  final _Engine _engine;
  final Pointer<_MpStatus> _statusBuffer = calloc<_MpStatus>();
  final Pointer<Float> _scopeBuffer = calloc<Float>(_scopeCapacity * 2);

  final void Function(_Engine) _destroy;
  final Pointer<Utf8> Function(_Engine) _lastError;
  final Pointer<Utf8> Function(_Engine) _backends;
  final Pointer<Utf8> Function(_Engine, Pointer<Utf8>) _collect;
  final int Function(_Engine, Pointer<Utf8>) _play;
  final void Function(_Engine) _stop;
  final void Function(_Engine, int) _setPaused;
  final void Function(_Engine, double) _seek;
  final void Function(_Engine, int) _selectSubsong;
  final void Function(_Engine, double) _setVolume;
  final void Function(_Engine, double) _setMegaSound;
  final void Function(_Engine, Pointer<_MpStatus>) _status;
  final Pointer<Utf8> Function(_Engine) _trackInfo;
  final int Function(_Engine, Pointer<Float>, int) _scope;

  T _withString<T>(String text, T Function(Pointer<Utf8>) body) {
    final native = text.toNativeUtf8();
    try {
      return body(native);
    } finally {
      calloc.free(native);
    }
  }

  CoreException _error() => CoreException(_lastError(_engine).toDartString());

  @override
  List<Backend> backends() => (jsonDecode(_backends(_engine).toDartString()) as List)
      .map((item) => Backend.fromJson(item as Map<String, dynamic>))
      .toList();

  @override
  List<Track> collect(String path) {
    final json = _withString(path, (native) => _collect(_engine, native));
    if (json == nullptr) throw _error();
    return (jsonDecode(json.toDartString()) as List)
        .map((item) => Track.fromJson(item as Map<String, dynamic>))
        .toList();
  }

  @override
  void play(String ref) {
    if (_withString(ref, (native) => _play(_engine, native)) != 0) throw _error();
  }

  @override
  void stop() => _stop(_engine);

  @override
  void setPaused(bool paused) => _setPaused(_engine, paused ? 1 : 0);

  @override
  void seek(double seconds) => _seek(_engine, seconds);

  @override
  void selectSubsong(int index) => _selectSubsong(_engine, index);

  @override
  void setVolume(double volume) => _setVolume(_engine, volume);

  @override
  void setMegaSound(double amount) => _setMegaSound(_engine, amount);

  @override

  @override
  CoreStatus status() {
    _status(_engine, _statusBuffer);
    final raw = _statusBuffer.ref;
    return CoreStatus(
      state: PlaybackState.values[raw.state],
      position: raw.position,
      duration: raw.duration < 0 ? null : raw.duration,
      subsong: raw.subsong,
      subsongCount: raw.subsongCount,
    );
  }

  @override
  TrackDetails details() =>
      TrackDetails.fromJson(jsonDecode(_trackInfo(_engine).toDartString()) as Map<String, dynamic>);

  @override
  Float32List scope(int frames) {
    final got = _scope(_engine, _scopeBuffer, frames.clamp(0, _scopeCapacity));
    return Float32List.fromList(_scopeBuffer.asTypedList(got * 2));
  }

  @override
  void dispose() {
    _destroy(_engine);
    calloc.free(_statusBuffer);
    calloc.free(_scopeBuffer);
  }
}
