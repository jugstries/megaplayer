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

/*
 * I moduli Roland emulati da 88emu, per i MIDI. Le ROM le fornisce l'utente:
 * mp_add_roland_rom_dir aggiunge una cartella in cui cercarle (con le
 * sottocartelle; quella di 88emuPlayer c'è già), mp_set_roland_device sceglie
 * il Sound Canvas per i MIDI General MIDI e GS ("" o NULL: il primo con le
 * ROM; i brani per MT-32 vanno comunque a un modulo della sua famiglia).
 * Valgono per tutto il processo, anche per la libreria. Senza 88emu nella
 * build non fanno niente.
 */
MP_API void mp_add_roland_rom_dir(const char* path);
MP_API void mp_set_roland_device(const char* name);

/*
 * Il SoundFont con cui FluidSynth suona i MIDI quando non ci sono le ROM dei
 * moduli Roland: di serie GeneralUser GS, se la build lo trova; "" o NULL lo
 * spengono, e i MIDI vanno all'FM di adlmidi. mp_engine_soundfont dice quello
 * in uso ("" se nessuno). Vale per tutto il processo.
 */
MP_API void mp_set_soundfont(const char* path);

/* Apre il dispositivo audio. NULL se non ci riesce. */
MP_API mp_engine* mp_engine_create(int32_t sample_rate);
MP_API void mp_engine_destroy(mp_engine* engine);

/* L'errore dell'ultima funzione fallita. */
MP_API const char* mp_engine_last_error(mp_engine* engine);

/* [{"name": "uade", "description": "..."}, ...] */
MP_API const char* mp_engine_backends(mp_engine* engine);

/*
 * I moduli Roland che la build conosce, in ordine di preferenza, e se le loro
 * ROM ci sono: [{"name": "SC-55mkII", "family": "gs", "available": true},
 * {"name": "MT-32 1.x (experimental)", "family": "la", ...}, ...]
 */
MP_API const char* mp_engine_roland_devices(mp_engine* engine);

/* Il SoundFont in uso per i MIDI, "" se nessuno (vedi mp_set_soundfont). */
MP_API const char* mp_engine_soundfont(mp_engine* engine);

/*
 * Brani contenuti in un file, un archivio o "ARCHIVIO/percorso/interno":
 * [{"path": "...", "ref": "...", "name": "...", "archive": true}, ...].
 * "path" è il file estratto, che resta su disco finché l'engine vive; "ref"
 * ritrova il brano anche dopo (il percorso, o "ARCHIVIO/percorso/interno").
 * NULL in caso di errore.
 */
MP_API const char* mp_engine_collect(mp_engine* engine, const char* path);

/*
 * Apre e avvia un brano: un percorso o un "ref". Per un brano dentro un
 * archivio non ancora estratto, estrae la sua cartella. 0 se riesce.
 */
MP_API int32_t mp_engine_play(mp_engine* engine, const char* ref);
MP_API void mp_engine_stop(mp_engine* engine);
MP_API void mp_engine_set_paused(mp_engine* engine, int32_t paused);
MP_API void mp_engine_seek(mp_engine* engine, double seconds);
MP_API void mp_engine_select_subsong(mp_engine* engine, int32_t index);
MP_API void mp_engine_set_volume(mp_engine* engine, float volume);

/* La manopola MegaSound, da 0 (spenta) a 1 (tutti gli effetti). */
MP_API void mp_engine_set_megasound(mp_engine* engine, float amount);

/*
 * Zittisce o riaccende un canale del brano corrente (un indice di
 * "channels" in mp_engine_track_info). Vale fino al brano dopo, anche
 * cambiando sottobrano.
 */
MP_API void mp_engine_set_channel_muted(mp_engine* engine, int32_t channel, int32_t muted);

/*
 * Esporta un brano in WAV o MP3 (dall'estensione di out_path) come lo suona
 * il player: il sottobrano [subsong] (base 0), i canali di [muted] zittiti e
 * la MegaSound a [megasound] (0..1). [title], [artist] e [album] vanno nei tag
 * dell'MP3 (NULL o "" per nessuno). Gira in un thread suo, fino alla fine del
 * brano (al più un quarto d'ora per quelli che non lo dicono); la
 * riproduzione si ferma prima, perché UADE suona un brano per volta. 0, o -1
 * in caso di errore (mp_engine_last_error).
 */
MP_API int32_t mp_engine_export(mp_engine* engine, const char* ref, int32_t subsong, const char* out_path,
                                float megasound, const int32_t* muted, int32_t muted_count, const char* title,
                                const char* artist, const char* album);

typedef struct mp_export_status {
    int32_t running;   /* 1 mentre scrive */
    int32_t failed;    /* 1 se è fallita: mp_engine_export_error dice perché */
    int32_t cancelled; /* 1 se è stata annullata */
    int32_t reserved;
    double seconds;    /* scritti finora */
    double total;      /* da scrivere, -1 se non si sa */
} mp_export_status;

MP_API void mp_engine_export_status(mp_engine* engine, mp_export_status* out);
MP_API const char* mp_engine_export_error(mp_engine* engine);
/* Si ferma e cancella il file. */
MP_API void mp_engine_export_cancel(mp_engine* engine);
/* 1 se la build sa scrivere MP3. */
MP_API int32_t mp_can_export_mp3(void);

MP_API void mp_engine_status(mp_engine* engine, mp_status* out);

/*
 * Il brano corrente: {"file", "packers", "backend", "format", "player",
 * "title", "author", "subsongCount", "currentSubsong", "duration",
 * "channels"}. "channels" sono i canali che si possono zittire, con il loro
 * nome ("" se non ne hanno uno); vuoto per i file audio.
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
 * `folder` limita la ricerca a una cartella rispetto alla radice ("" per
 * tutta la libreria), con le sottocartelle se `subfolders` non è 0.
 * {"total": 120, "tracks": [{"ref": "...", "name": "...", "folder": "...",
 *   "backend": "...", "format": "...", "player": "...", "platform": "amiga",
 *   "platformName": "Amiga", "title": "...", "author": "...", "subsongs": 1,
 *   "startSubsong": 0, "duration": 61.2 | null, ...}]}
 */
MP_API const char* mp_library_search(mp_library* library, const char* text, const char* platforms,
                                     const char* formats, const char* folder, int32_t subfolders, int32_t offset,
                                     int32_t limit);
/*
 * Quanti brani per piattaforma e per formato, contati con il testo, la
 * cartella (sottocartelle comprese) e il filtro dell'altro gruppo:
 * {"platforms": [{"id": "amiga", "name": "Amiga", "count": 12}],
 *  "formats": [{"name": "ProTracker", "count": 3}]}
 */
MP_API const char* mp_library_facets(mp_library* library, const char* text, const char* platforms,
                                     const char* formats, const char* folder);
/*
 * Le sottocartelle di `folder` con brani della ricerca, e quanti ne hanno
 * in tutto: [{"name": "Jesper Kyd", "count": 12}, ...].
 */
MP_API const char* mp_library_folders(mp_library* library, const char* text, const char* platforms,
                                      const char* formats, const char* folder);
/* Un brano, con "durations": la durata di ogni sottobrano (null se non si sa). "" se non c'è. */
MP_API const char* mp_library_track(mp_library* library, const char* ref);

/*
 * Playlist. La coda è una playlist nascosta (mp_library_queue). Le voci sono
 * brani interi o un loro sottobrano; lo stesso brano entra più volte solo
 * con sottobrani diversi. Le funzioni che restituiscono un intero danno -1
 * in caso di errore (mp_library_last_error).
 */
/* [{"id": 3, "name": "Preferiti", "count": 12}, ...], senza la coda. */
MP_API const char* mp_library_playlists(mp_library* library);
MP_API int64_t mp_library_playlist_create(mp_library* library, const char* name);
MP_API int32_t mp_library_playlist_rename(mp_library* library, int64_t id, const char* name);
MP_API int32_t mp_library_playlist_delete(mp_library* library, int64_t id);
/* L'id della coda. */
MP_API int64_t mp_library_queue(mp_library* library);
/*
 * [{"ref": "...", "subsong": 2 | null, "name": "...", "track": {...} | null}]:
 * subsong null è il file intero; "track" ha i dati della libreria (come in
 * mp_library_search) se il brano c'è. NULL in caso di errore.
 */
MP_API const char* mp_library_playlist_entries(mp_library* library, int64_t id);
/*
 * Sostituisce le voci: una per riga, "SOTTOBRANO\tREF\tNOME", con il
 * sottobrano vuoto per il file intero. I doppioni si scartano.
 */
MP_API int32_t mp_library_playlist_set_entries(mp_library* library, int64_t id, const char* entries);

/* Impostazioni dell'app (skin, volume, ...): "" se mai scritte. */
MP_API const char* mp_library_setting(mp_library* library, const char* key);
MP_API int32_t mp_library_set_setting(mp_library* library, const char* key, const char* value);

#ifdef __cplusplus
}
#endif

#endif
