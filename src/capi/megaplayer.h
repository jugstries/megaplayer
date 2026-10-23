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

#ifdef __cplusplus
}
#endif

#endif
