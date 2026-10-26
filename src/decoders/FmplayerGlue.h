#ifndef MP_FMPLAYER_GLUE_H
#define MP_FMPLAYER_GLUE_H

/* Uno strato in C attorno a 98fmplayer, i cui header non sono C++ (struct
 * annidate): un brano PMD o FMP con il suo YM2608, dietro un puntatore
 * opaco. Usato da FmplayerDecoder.cpp. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MP_FM_PMD, MP_FM_FMP };

/* La frequenza a cui genera lo YM2608: 7987200 / 144 Hz. */
#define MP_FM_RATE 55467

typedef struct mp_fm_song mp_fm_song;

/* Copia il brano e lo fa leggere al driver; NULL se non è suo. */
mp_fm_song *mp_fm_create(int driver, const uint8_t *data, size_t size);
void mp_fm_free(mp_fm_song *song);

/* Quanto dura, facendo girare il driver su un OPNA finto senza sintetizzare:
 * fino alla fine, al giro `loops` del loop o a `max_seconds`. *looped dice
 * se si è fermato per il loop. Il brano non cambia. */
double mp_fm_measure(const mp_fm_song *song, int loops, double max_seconds, bool *looped);

/* Prepara lo YM2608 (con la ROM della batteria, se c'è: 8 KB) e avvia il
 * driver. */
void mp_fm_start(mp_fm_song *song, const uint8_t *drum_rom);

/* I PCM che il brano vuole: 0 l'ADPCM (PPC del PMD, PVI dell'FMP), 1 e 2 i
 * banchi del PPZ8. "" se non ne vuole. */
const char *mp_fm_pcm_name(const mp_fm_song *song, int which);
bool mp_fm_load_adpcm(mp_fm_song *song, const uint8_t *data, size_t size);
/* pzi: false per un PVI, true per un PZI. */
bool mp_fm_load_ppz(mp_fm_song *song, int bank, const uint8_t *data, size_t size, bool pzi);

/* Scrive frames frame stereo. */
void mp_fm_mix(mp_fm_song *song, int16_t *buffer, unsigned frames);

/* Per il seek: se il driver suona ancora, quanto dura un tick col tempo di
 * adesso, un tick senza sintetizzare, e il timer che riparte da capo. */
bool mp_fm_playing(const mp_fm_song *song);
double mp_fm_tick_seconds(const mp_fm_song *song);
void mp_fm_tick(mp_fm_song *song);
void mp_fm_restart_timer(mp_fm_song *song);

/* I canali zittiti, un bit ciascuno: FM 1-6, SSG 1-3, batteria, ADPCM,
 * PPZ8 1-8. */
void mp_fm_set_mute(mp_fm_song *song, unsigned muted);

/* I commenti in Shift-JIS, con le sequenze ANSI; NULL se la riga non c'è.
 * Nel PMD la riga 0 è il titolo e la 1 il compositore. */
const char *mp_fm_comment(mp_fm_song *song, int line);
bool mp_fm_comment_is_pmd(const mp_fm_song *song);

/* Lo YM2608 da solo, senza driver, per i log S98: le scritture ai registri
 * (0x100 in più per la seconda porta) e il suono a MP_FM_RATE per un chip a
 * 7,9872 MHz (a un'altra frequenza, la stessa proporzione). */
typedef struct mp_opna_chip mp_opna_chip;
mp_opna_chip *mp_opna_create(const uint8_t *drum_rom);
void mp_opna_free(mp_opna_chip *chip);
void mp_opna_write(mp_opna_chip *chip, unsigned reg, unsigned value);
void mp_opna_mix(mp_opna_chip *chip, int16_t *buffer, unsigned frames);
/* I canali zittiti, come in mp_fm_set_mute senza il PPZ8. */
void mp_opna_set_mute(mp_opna_chip *chip, unsigned muted);

#ifdef __cplusplus
}
#endif

#endif
