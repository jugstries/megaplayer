// L'API C di megaplayer (src/capi/megaplayer.h) per il browser, sopra il
// modulo WebAssembly megaplayer_web. Le stringhe restituite dal core sono
// JSON; il modulo ha memoria fissa, quindi le viste HEAP* restano valide.

/* global createMegaplayer */

async function openMegaplayer({ sampleRate = 48000 } = {}) {
  const M = await createMegaplayer();

  const text = (pointer) => M.UTF8ToString(pointer);
  const withString = (value, body) => {
    const pointer = M.stringToNewUTF8(value);
    try {
      return body(pointer);
    } finally {
      M._free(pointer);
    }
  };

  // I replayer di UADE sono nel filesystem virtuale (vedi CMakeLists.txt).
  withString('/uade', (p) => M._mp_set_uade_data_dir(p));
  const engine = M._mp_engine_create(sampleRate);
  if (!engine) throw new Error("impossibile aprire l'audio");

  const lastError = () => new Error(text(M._mp_engine_last_error(engine)));
  const statusBuffer = M._malloc(32);
  const scopeCapacity = 4096;
  const scopeBuffer = M._malloc(scopeCapacity * 2 * 4);
  M.FS.mkdirTree('/files');

  return {
    // Copia un file scelto dall'utente nel filesystem virtuale.
    async addFile(file) {
      const path = `/files/${file.name}`;
      M.FS.writeFile(path, new Uint8Array(await file.arrayBuffer()));
      return path;
    },
    backends: () => JSON.parse(text(M._mp_engine_backends(engine))),
    collect(path) {
      const json = withString(path, (p) => M._mp_engine_collect(engine, p));
      if (!json) throw lastError();
      return JSON.parse(text(json));
    },
    play(path) {
      if (withString(path, (p) => M._mp_engine_play(engine, p)) !== 0) throw lastError();
    },
    stop: () => M._mp_engine_stop(engine),
    setPaused: (paused) => M._mp_engine_set_paused(engine, paused ? 1 : 0),
    seek: (seconds) => M._mp_engine_seek(engine, seconds),
    selectSubsong: (index) => M._mp_engine_select_subsong(engine, index),
    setVolume: (volume) => M._mp_engine_set_volume(engine, volume),
    status() {
      M._mp_engine_status(engine, statusBuffer);
      const ints = statusBuffer >> 2;
      const doubles = (statusBuffer + 16) >> 3;
      const duration = M.HEAPF64[doubles + 1];
      return {
        state: ['idle', 'playing', 'paused', 'ended'][M.HEAP32[ints]],
        subsong: M.HEAP32[ints + 1],
        subsongCount: M.HEAP32[ints + 2],
        position: M.HEAPF64[doubles],
        duration: duration < 0 ? null : duration,
      };
    },
    trackInfo: () => JSON.parse(text(M._mp_engine_track_info(engine))),
    // Gli ultimi frame stereo arrivati alle casse: [L, R, L, R, ...].
    scope(frames) {
      const got = M._mp_engine_scope(engine, scopeBuffer, Math.min(frames, scopeCapacity));
      return M.HEAPF32.slice(scopeBuffer >> 2, (scopeBuffer >> 2) + got * 2);
    },
  };
}
