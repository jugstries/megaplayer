/*
 * API C del core di megaplayer, per le interfacce grafiche (Flutter via
 * dart:ffi) e in futuro per WebAssembly.
 *
 * Tutte le funzioni di un engine vanno chiamate da un solo thread. Le
 * stringhe restituite sono UTF-8, appartengono all'engine e restano valide
 * fino alla chiamata successiva della stessa funzione.
 */
#ifndef MEGAPLAYER_H
#define MEGAPLAYER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define MP_API __declspec(dllexport)
#else
#define MP_API __attribute__((visibility("default")))
#endif

typedef struct mp_engine mp_engine;

enum {
    MP_IDLE = 0,
    MP_PLAYING = 1,
    MP_PAUSED = 2,
    MP_ENDED = 3,
};

typedef struct mp_status {
    int32_t state;         /* MP_IDLE, MP_PLAYING, ... */
    int32_t subsong;       /* base 0 */
    int32_t subsong_count;
    int32_t reserved;
    double position;       /* secondi */
    double duration;       /* secondi, negativa se sconosciuta */
} mp_status;

/*
 * Cartella con replayer e configurazione di UADE, se non è quella scelta alla
 * compilazione (un'app impacchettata, WebAssembly). Va chiamata prima di
 * mp_engine_create.
 */
MP_API void mp_set_uade_data_dir(const char* path);

/* Apre il dispositivo audio. NULL se non ci riesce. */
MP_API mp_engine* mp_engine_create(int32_t sample_rate);
MP_API void mp_engine_destroy(mp_engine* engine);

/* L'errore dell'ultima funzione fallita. */
MP_API const char* mp_engine_last_error(mp_engine* engine);

/* [{"name": "uade", "description": "..."}, ...] */
MP_API const char* mp_engine_backends(mp_engine* engine);

/*
 * Brani contenuti in un file, un archivio o "ARCHIVIO/percorso/interno":
 * [{"path": "...", "name": "...", "archive": true}, ...]. I file estratti
 * restano su disco finché l'engine vive. NULL in caso di errore.
 */
MP_API const char* mp_engine_collect(mp_engine* engine, const char* path);

/* Apre e avvia un brano (un "path" di mp_engine_collect). 0 se riesce. */
MP_API int32_t mp_engine_play(mp_engine* engine, const char* path);
MP_API void mp_engine_stop(mp_engine* engine);
MP_API void mp_engine_set_paused(mp_engine* engine, int32_t paused);
MP_API void mp_engine_seek(mp_engine* engine, double seconds);
MP_API void mp_engine_select_subsong(mp_engine* engine, int32_t index);
MP_API void mp_engine_set_volume(mp_engine* engine, float volume);

/* La manopola MegaSound, da 0 (spenta) a 1 (tutti gli effetti). */
MP_API void mp_engine_set_megasound(mp_engine* engine, float amount);


MP_API void mp_engine_status(mp_engine* engine, mp_status* out);

/*
 * Il brano corrente: {"file", "packers", "backend", "format", "player",
 * "title", "author", "subsongCount", "currentSubsong", "duration"}.
 */
MP_API const char* mp_engine_track_info(mp_engine* engine);

/*
 * Gli ultimi `frames` frame stereo (float interleaved) arrivati alle casse,
 * per gli oscilloscopi. Restituisce quanti frame ha scritto in `out`.
 */
MP_API int32_t mp_engine_scope(mp_engine* engine, float* out, int32_t frames);

/*
 * La libreria: l'indice di una cartella radice, in un database SQLite, con
 * ricerca e filtri per piattaforma e formato. Non c'è nella build
 * WebAssembly. Come per l'engine, le funzioni vanno chiamate da un solo
 * thread; la scansione gira in un thread suo.
 *
 * I brani si identificano con "ref": il percorso del file, o
 * "ARCHIVIO/percorso/interno" per un brano dentro un archivio, che
 * mp_engine_collect accetta così com'è.
 */
typedef struct mp_library mp_library;

typedef struct mp_scan_status {
    int32_t running;       /* 1 mentre la scansione è in corso */
    int32_t walking;       /* 1 mentre conta ancora i file della cartella */
    int32_t files_found;   /* file nella cartella */
    int32_t files_checked; /* file controllati, già noti o letti */
    int32_t files_read;    /* file nuovi o cambiati, aperti */
    int32_t tracks_found;  /* brani nei file letti */
    int32_t files_removed; /* file spariti tolti dall'indice, a scansione finita */
    int32_t failed;        /* 1 se l'ultima scansione è fallita: mp_library_last_error */
} mp_scan_status;

/* Apre o crea l'indice; path NULL o "" per quello nella cartella dei dati dell'utente. */
MP_API mp_library* mp_library_open(const char* path);
/* Ferma la scansione in corso e chiude. */
MP_API void mp_library_close(mp_library* library);
MP_API const char* mp_library_last_error(mp_library* library);
/* Il percorso del database. */
MP_API const char* mp_library_database(mp_library* library);

/* La cartella radice, "" se non è stata scelta. */
MP_API const char* mp_library_root(mp_library* library);
/* Cambiarla svuota l'indice. -1 durante una scansione o in caso di errore. */
MP_API int32_t mp_library_set_root(mp_library* library, const char* path);

/*
 * Avvia la scansione in background: legge solo i file nuovi o cambiati e
 * toglie quelli spariti. -1 se ne è già in corso una o manca la radice.
 */
MP_API int32_t mp_library_scan(mp_library* library);
MP_API void mp_library_cancel_scan(mp_library* library);
MP_API void mp_library_scan_status(mp_library* library, mp_scan_status* out);

/*
 * Ricerca: le parole di `text` in titolo, autore, nome e cartella;
 * `platforms` e `formats` separati da '\n', vuoti per non filtrare.
 * {"total": 120, "tracks": [{"ref": "...", "name": "...", "folder": "...",
 *   "backend": "...", "format": "...", "player": "...", "platform": "amiga",
 *   "platformName": "Amiga", "title": "...", "author": "...", "subsongs": 1,
 *   "startSubsong": 0, "duration": 61.2 | null, ...}]}
 */
MP_API const char* mp_library_search(mp_library* library, const char* text, const char* platforms,
                                     const char* formats, int32_t offset, int32_t limit);
/*
 * Quanti brani per piattaforma e per formato, contati con il testo e con il
 * filtro dell'altro gruppo:
 * {"platforms": [{"id": "amiga", "name": "Amiga", "count": 12}],
 *  "formats": [{"name": "ProTracker", "count": 3}]}
 */
MP_API const char* mp_library_facets(mp_library* library, const char* text, const char* platforms,
                                     const char* formats);
/* Un brano, con "durations": la durata di ogni sottobrano (null se non si sa). "" se non c'è. */
MP_API const char* mp_library_track(mp_library* library, const char* ref);

#ifdef __cplusplus
}
#endif

#endif
