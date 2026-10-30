/* L3SubBand */

#include "l3subband.h"
#include "tables.h"
#include "types.h"

/*
 * shine_subband_initialise:
 * ----------------------
 * Calculates the analysis filterbank coefficients and rounds to the
 * 9th decimal place accuracy of the filterbank tables in the ISO
 * document.  The coefficients are stored in #filter#
 */
/* 2026-10-07 (gbamp3): the filter (fl, folded: shine_fl32) comes from
 * shine_gen_tables.h. */
#include "shine_gen_tables.h"
#if defined(__arm__)
void shine_window_arm(const int32_t *x, unsigned off, int32_t *y, const int32_t *w);
void shine_matrix_arm(const int32_t *u, int32_t *s, const int32_t (*f)[32]);
#endif
void shine_subband_initialise(shine_global_config *config) {
  int i;

  for (i = MAX_CHANNELS; i--;) {
    config->subband.off[i] = 0;
    memset(config->subband.x[i], 0, sizeof(config->subband.x[i]));
  }
}

/*
 * shine_window_filter_subband:
 * -------------------------
 * Overlapping window on PCM samples
 * 32 16-bit pcm samples are scaled to fractional 2's complement and
 * concatenated to the end of the window buffer #x#. The updated window
 * buffer #x# is then windowed by the analysis window #shine_enwindow# to
 * produce the windowed sample #z# Calculates the analysis filter bank
 * coefficients The windowed samples #z# is filtered by the digital filter
 * matrix #filter# to produce the subband samples #s#. This done by first
 * selectively picking out values from the windowed samples, and then
 * multiplying them by the filter matrix, producing 32 subband samples.
 */
/* 2026-10-07 (gbamp3): y and the folded u static, not on the stack (one
 * encoder at a time; GBA: the encoder overlay's scratch). */
#if defined(__arm__)
static int32_t y[64] __attribute__((section(".menc_scratch"), aligned(4)));
static int32_t u[32] __attribute__((section(".menc_scratch"), aligned(4)));
#else
static int32_t y[64], u[32];
#endif
void shine_window_filter_subband(int16_t **buffer, int32_t s[SBLIMIT], int ch,
                                 shine_global_config *config, int stride) {
  int i, j;
  int16_t *ptr = *buffer;

  /* replace 32 oldest samples with 32 new samples */
  for (i = 32; i--;) {
    config->subband.x[ch][i + config->subband.off[ch]] = ((int32_t)*ptr) << 16;
    ptr += stride;
  }
  *buffer = ptr;

#if defined(__arm__)
  /* 2026-10-07 (gbamp3): ARM routines (player/src/mp3enc_arm.S), same results */
  shine_window_arm(config->subband.x[ch], config->subband.off[ch], y, shine_enwindow);
#else
  for (i = 64; i--;) {
    int32_t s_value;
#ifdef __BORLANDC__
    uint32_t s_value_lo;
#else
    uint32_t s_value_lo __attribute__((unused));
#endif

    mul0(s_value, s_value_lo,
         config->subband
             .x[ch][(config->subband.off[ch] + i + (0 << 6)) & (HAN_SIZE - 1)],
         shine_enwindow[i + (0 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (1 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (1 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (2 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (2 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (3 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (3 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (4 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (4 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (5 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (5 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (6 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (6 << 6)]);
    muladd(s_value, s_value_lo,
           config->subband.x[ch][(config->subband.off[ch] + i + (7 << 6)) &
                                 (HAN_SIZE - 1)],
           shine_enwindow[i + (7 << 6)]);
    mulz(s_value, s_value_lo);
    y[i] = s_value;
  }

#endif
  config->subband.off[ch] = (config->subband.off[ch] + 480) &
                            (HAN_SIZE - 1); /* offset is modulo (HAN_SIZE)*/

  /* 2026-10-07 (gbamp3): the matrix folded by its symmetries (32 columns,
   * shine_gen_tables.h); |y| < 2^26, so the sums are exact. */
  {
    u[0] = y[0] + y[32];
    for (j = 1; j < 16; j++)
      u[j] = y[j] + y[32 - j];
    u[16] = y[16];
    for (j = 17; j < 32; j++)
      u[j] = y[j + 16] - y[80 - j];
#if defined(__arm__)
    shine_matrix_arm(u, s, shine_fl32);
#else
    for (i = SBLIMIT; i--;) {
      int32_t s_value;
      uint32_t s_value_lo __attribute__((unused));
      const int32_t *f = shine_fl32[i];

      mul0(s_value, s_value_lo, f[31], u[31]);
      for (j = 31; j; j -= 5) {
        muladd(s_value, s_value_lo, f[j - 1], u[j - 1]);
        muladd(s_value, s_value_lo, f[j - 2], u[j - 2]);
        muladd(s_value, s_value_lo, f[j - 3], u[j - 3]);
        muladd(s_value, s_value_lo, f[j - 4], u[j - 4]);
        muladd(s_value, s_value_lo, f[j - 5], u[j - 5]);
        if (j == 6) { muladd(s_value, s_value_lo, f[0], u[0]); break; }
      }
      mulz(s_value, s_value_lo);
      s[i] = s_value;
    }
#endif
  }
}
