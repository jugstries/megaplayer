#include "FmplayerGlue.h"

#include "fmdriver/fmdriver.h"
#include "fmdriver/fmdriver_fmp.h"
#include "fmdriver/fmdriver_pmd.h"
#include "fmdriver/ppz8.h"
#include "libopna/opna.h"
#include "libopna/opnadrum.h"
#include "libopna/opnatimer.h"

#include <stdlib.h>
#include <string.h>

enum {
  PPZ8_MIX = 0xa000,
  ADPCM_RAM = 256 * 1024,
  STOPPED = 0xff, /* loop_cnt di un brano finito senza loop */
};

/* Tutto in un posto che non si sposta: i driver tengono puntatori ai dati. */
struct mp_fm_song {
  int driver;
  uint8_t *data;
  size_t size;
  struct driver_pmd pmd;
  struct driver_fmp fmp;
  struct fmdriver_work work;
  struct opna chip;
  struct opna_timer timer;
  struct ppz8 pcm;
  uint8_t adpcm_ram[ADPCM_RAM];
  int16_t *ppz_buffers[2];
};

static bool load(mp_fm_song *song) {
  uint16_t size = (uint16_t)song->size;
  return song->driver == MP_FM_PMD ? pmd_load(&song->pmd, song->data, size)
                                   : fmp_load(&song->fmp, song->data, size);
}

static void start_driver(mp_fm_song *song) {
  if (song->driver == MP_FM_PMD) pmd_init(&song->work, &song->pmd);
  else fmp_init(&song->work, &song->fmp);
}

mp_fm_song *mp_fm_create(int driver, const uint8_t *data, size_t size) {
  if (!size || size > 0xffff) return NULL;
  mp_fm_song *song = calloc(1, sizeof(*song));
  if (!song) return NULL;
  song->driver = driver;
  song->size = size;
  song->data = malloc(size);
  if (!song->data) {
    free(song);
    return NULL;
  }
  memcpy(song->data, data, size);
  if (!load(song)) {
    mp_fm_free(song);
    return NULL;
  }
  return song;
}

void mp_fm_free(mp_fm_song *song) {
  if (!song) return;
  free(song->ppz_buffers[0]);
  free(song->ppz_buffers[1]);
  free(song->data);
  free(song);
}

/* L'OPNA finto di mp_fm_measure: niente suono, solo i flag dei timer in
 * work->opna, che il driver legge finché non li spegne scrivendo il registro
 * 0x27, come col chip vero. */
static void dummy_writereg(struct fmdriver_work *work, unsigned address, unsigned data) {
  uint8_t *status = work->opna;
  if (address == 0x27) {
    if (data & 0x10) *status &= ~1;
    if (data & 0x20) *status &= ~2;
  }
}
static unsigned dummy_readreg(struct fmdriver_work *work, unsigned address) {
  (void)work; (void)address;
  return 0xff;
}
static uint8_t dummy_status(struct fmdriver_work *work, bool a1) {
  (void)a1;
  return *(uint8_t *)work->opna;
}

/* 16 campioni per passo del timer B (opnatimer.c). */
static double tick_seconds(const struct fmdriver_work *work) {
  return 16.0 * (256 - work->timerb) / MP_FM_RATE;
}

double mp_fm_measure(const mp_fm_song *original, int loops, double max_seconds, bool *looped) {
  *looped = false;
  mp_fm_song *song = mp_fm_create(original->driver, original->data, original->size);
  if (!song) return 0;
  uint8_t status = 0;
  song->work.opna_writereg = dummy_writereg;
  song->work.opna_readreg = dummy_readreg;
  song->work.opna_status = dummy_status;
  song->work.opna = &status;
  start_driver(song);
  double seconds = 0;
  while (seconds < max_seconds) {
    status |= 2; /* scatta il timer B */
    song->work.driver_opna_interrupt(&song->work);
    status = 0;
    seconds += tick_seconds(&song->work);
    if (!song->work.playing || song->work.loop_cnt == STOPPED) break;
    if (song->work.loop_cnt >= loops) {
      *looped = true;
      break;
    }
  }
  mp_fm_free(song);
  return seconds;
}

/* Lo YM2608 emulato, come in common/fmplayer_work_opna.c. */
static void opna_writereg_libopna(struct fmdriver_work *work, unsigned address, unsigned data) {
  opna_timer_writereg((struct opna_timer *)work->opna, address, data);
}
static unsigned opna_readreg_libopna(struct fmdriver_work *work, unsigned address) {
  return opna_readreg(((struct opna_timer *)work->opna)->opna, address);
}
static uint8_t opna_status_libopna(struct fmdriver_work *work, bool a1) {
  uint8_t status = opna_timer_status((struct opna_timer *)work->opna);
  return a1 ? status : status & 0x83;
}
static void interrupt(void *user) {
  struct fmdriver_work *work = user;
  work->driver_opna_interrupt(work);
}
static void mix_ppz8(void *user, int16_t *buffer, unsigned samples) {
  ppz8_mix(user, buffer, samples);
}

void mp_fm_start(mp_fm_song *song, const uint8_t *drum_rom) {
  opna_reset(&song->chip);
  if (drum_rom) opna_drum_set_rom(&song->chip.drum, (void *)drum_rom);
  opna_adpcm_set_ram_256k(&song->chip.adpcm, song->adpcm_ram);
  opna_timer_reset(&song->timer, &song->chip);
  ppz8_init(&song->pcm, MP_FM_RATE, PPZ8_MIX);
  memset(&song->work, 0, sizeof(song->work));
  song->work.opna_writereg = opna_writereg_libopna;
  song->work.opna_readreg = opna_readreg_libopna;
  song->work.opna_status = opna_status_libopna;
  song->work.opna = &song->timer;
  song->work.ppz8 = &song->pcm;
  song->work.ppz8_functbl = &ppz8_functbl;
  opna_timer_set_int_callback(&song->timer, interrupt, &song->work);
  opna_timer_set_mix_callback(&song->timer, mix_ppz8, &song->pcm);
  start_driver(song);
}

const char *mp_fm_pcm_name(const mp_fm_song *song, int which) {
  if (song->driver == MP_FM_PMD) {
    switch (which) {
    case 0: return song->pmd.ppcfile;
    case 1: return song->pmd.ppzfile;
    case 2: return song->pmd.ppzfile2;
    }
  } else {
    switch (which) {
    case 0: return song->fmp.pvi_name;
    case 1: return song->fmp.ppz_name;
    }
  }
  return "";
}

bool mp_fm_load_adpcm(mp_fm_song *song, const uint8_t *data, size_t size) {
  uint8_t *copy = malloc(size);
  if (!copy) return false;
  memcpy(copy, data, size);
  bool loaded = song->driver == MP_FM_PMD ? pmd_ppc_load(&song->work, copy, size)
                                          : fmp_adpcm_load(&song->work, copy, size);
  free(copy);
  return loaded;
}

bool mp_fm_load_ppz(mp_fm_song *song, int bank, const uint8_t *data, size_t size, bool pzi) {
  if (bank < 0 || bank > 1) return false;
  uint32_t samples = pzi ? ppz8_pzi_decodebuf_samples((uint32_t)size) : ppz8_pvi_decodebuf_samples((uint32_t)size);
  int16_t *buffer = calloc(samples ? samples : 1, sizeof(int16_t));
  if (!buffer) return false;
  bool loaded = pzi ? ppz8_pzi_load(&song->pcm, (uint8_t)bank, data, (uint32_t)size, buffer)
                    : ppz8_pvi_load(&song->pcm, (uint8_t)bank, data, (uint32_t)size, buffer);
  if (!loaded) {
    free(buffer);
    return false;
  }
  free(song->ppz_buffers[bank]);
  song->ppz_buffers[bank] = buffer;
  return true;
}

void mp_fm_mix(mp_fm_song *song, int16_t *buffer, unsigned frames) {
  memset(buffer, 0, frames * 2 * sizeof(int16_t));
  opna_timer_mix(&song->timer, buffer, frames);
}

bool mp_fm_playing(const mp_fm_song *song) {
  return song->work.playing;
}

double mp_fm_tick_seconds(const mp_fm_song *song) {
  return tick_seconds(&song->work);
}

void mp_fm_tick(mp_fm_song *song) {
  song->timer.status |= 2; /* come se fosse scattato il timer B */
  song->work.driver_opna_interrupt(&song->work);
}

void mp_fm_restart_timer(mp_fm_song *song) {
  song->timer.timerb_cnt = (uint16_t)(song->timer.timerb << 4);
}

enum { CHIP_CHANNELS = 11 };

/* I bit dei canali di megaplayer in quelli di libopna. */
static unsigned chip_mask(unsigned muted) {
  static const unsigned chip_channels[CHIP_CHANNELS] = {
    LIBOPNA_CHAN_FM_1, LIBOPNA_CHAN_FM_2, LIBOPNA_CHAN_FM_3, LIBOPNA_CHAN_FM_4,
    LIBOPNA_CHAN_FM_5, LIBOPNA_CHAN_FM_6, LIBOPNA_CHAN_SSG_1, LIBOPNA_CHAN_SSG_2,
    LIBOPNA_CHAN_SSG_3, LIBOPNA_CHAN_DRUM_ALL, LIBOPNA_CHAN_ADPCM,
  };
  unsigned mask = 0;
  for (unsigned i = 0; i < CHIP_CHANNELS; i++) {
    if (muted & (1u << i)) mask |= chip_channels[i];
  }
  return mask;
}

void mp_fm_set_mute(mp_fm_song *song, unsigned muted) {
  opna_set_mask(&song->chip, chip_mask(muted));
  ppz8_set_mask(&song->pcm, (muted >> CHIP_CHANNELS) & 0xff);
}

const char *mp_fm_comment(mp_fm_song *song, int line) {
  return song->work.get_comment ? song->work.get_comment(&song->work, line) : NULL;
}

bool mp_fm_comment_is_pmd(const mp_fm_song *song) {
  return song->work.comment_mode_pmd;
}

struct mp_opna_chip {
  struct opna chip;
  uint8_t adpcm_ram[ADPCM_RAM];
};

mp_opna_chip *mp_opna_create(const uint8_t *drum_rom) {
  mp_opna_chip *chip = calloc(1, sizeof(*chip));
  if (!chip) return NULL;
  opna_reset(&chip->chip);
  if (drum_rom) opna_drum_set_rom(&chip->chip.drum, (void *)drum_rom);
  opna_adpcm_set_ram_256k(&chip->chip.adpcm, chip->adpcm_ram);
  return chip;
}

void mp_opna_free(mp_opna_chip *chip) {
  free(chip);
}

void mp_opna_write(mp_opna_chip *chip, unsigned reg, unsigned value) {
  opna_writereg(&chip->chip, reg & 0x1ff, value & 0xff);
}

void mp_opna_mix(mp_opna_chip *chip, int16_t *buffer, unsigned frames) {
  memset(buffer, 0, frames * 2 * sizeof(int16_t));
  opna_mix(&chip->chip, buffer, frames);
}

void mp_opna_set_mute(mp_opna_chip *chip, unsigned muted) {
  opna_set_mask(&chip->chip, chip_mask(muted));
}
