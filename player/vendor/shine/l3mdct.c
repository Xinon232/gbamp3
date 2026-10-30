/* L3mdct */

#include "l3mdct.h"
#include "l3subband.h"
#include "types.h"

/* This is table B.9: coefficients for aliasing reduction.
 * 2026-10-07 (gbamp3): MDCT_CA*, MDCT_CS* and the combined window and MDCT
 * coefficients (cos_l) come from shine_gen_tables.h (computed at start-up
 * with sqrt / sin / cos before). */
#include "shine_gen_tables.h"

/*
 * shine_mdct_sub:
 * ------------
 */
/* 2026-10-07 (gbamp3): static (was on the stack). */
#if defined(__arm__)
static int32_t mdct_in[36] __attribute__((section(".menc_scratch"), aligned(4)));
#else
static int32_t mdct_in[36];
#endif
void shine_mdct_sub(shine_global_config *config, int stride) {
  /* note. we wish to access the array 'config->mdct_freq[2][2][576]' as
   * [2][2][32][18]. (32*18=576),
   */
  int32_t(*mdct_enc)[18];

  int ch, gr, band, j, k;

  for (ch = config->wave.channels; ch--;) {
    for (gr = 0; gr < config->mpeg.granules_per_frame; gr++) {
      /* set up pointer to the part of config->mdct_freq we're using */
      mdct_enc = (int32_t(*)[18])config->mdct_freq[ch][gr];

      /* polyphase filtering */
      for (k = 0; k < 18; k += 2) {
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k][0], ch,
                                    config, stride);
        shine_window_filter_subband(&config->buffer[ch],
                                    &config->l3_sb_sample[ch][gr + 1][k + 1][0],
                                    ch, config, stride);
        /* Compensate for inversion in the analysis filter
         * (every odd index of band AND k)
         */
        for (band = 1; band < 32; band += 2)
          config->l3_sb_sample[ch][gr + 1][k + 1][band] *= -1;
      }

      /* Perform imdct of 18 previous subband samples + 18 current subband
       * samples */
      for (band = 0; band < 32; band++) {
        for (k = 18; k--;) {
          mdct_in[k] = config->l3_sb_sample[ch][gr][k][band];
          mdct_in[k + 18] = config->l3_sb_sample[ch][gr + 1][k][band];
        }

        /* Calculation of the MDCT
         * In the case of long blocks ( block_type 0,1,3 ) there are
         * 36 coefficients in the time domain and 18 in the frequency
         * domain.
         */
        /* 2026-10-07 (gbamp3): window first, then the folded 18 x 18
         * transform (shine_gen_tables.h); was the 18 x 36 product with the
         * window inside the cosine table. */
        {
          int32_t v[18];
          for (j = 0; j < 9; j++) {
            v[j] = muls(mdct_in[j], shine_mdct_win[j]) - muls(mdct_in[17 - j], shine_mdct_win[17 - j]);
            v[j + 9] = muls(mdct_in[j + 18], shine_mdct_win[j + 18]) + muls(mdct_in[35 - j], shine_mdct_win[35 - j]);
          }
          for (k = 18; k--;) {
            int32_t vm;
            uint32_t vm_lo __attribute__((unused));
            const int32_t *c = shine_cos18[k];

            mul0(vm, vm_lo, v[17], c[17]);
            for (j = 17; j; j -= 4) {
              muladd(vm, vm_lo, v[j - 1], c[j - 1]);
              if (j == 1) break;
              muladd(vm, vm_lo, v[j - 2], c[j - 2]);
              muladd(vm, vm_lo, v[j - 3], c[j - 3]);
              muladd(vm, vm_lo, v[j - 4], c[j - 4]);
            }
            mulz(vm, vm_lo);
            mdct_enc[band][k] = vm;
          }
        }

        /* Perform aliasing reduction butterfly */
        if (band != 0) {
          cmuls(mdct_enc[band][0], mdct_enc[band - 1][17 - 0],
                mdct_enc[band][0], mdct_enc[band - 1][17 - 0], MDCT_CS0,
                MDCT_CA0);
          cmuls(mdct_enc[band][1], mdct_enc[band - 1][17 - 1],
                mdct_enc[band][1], mdct_enc[band - 1][17 - 1], MDCT_CS1,
                MDCT_CA1);
          cmuls(mdct_enc[band][2], mdct_enc[band - 1][17 - 2],
                mdct_enc[band][2], mdct_enc[band - 1][17 - 2], MDCT_CS2,
                MDCT_CA2);
          cmuls(mdct_enc[band][3], mdct_enc[band - 1][17 - 3],
                mdct_enc[band][3], mdct_enc[band - 1][17 - 3], MDCT_CS3,
                MDCT_CA3);
          cmuls(mdct_enc[band][4], mdct_enc[band - 1][17 - 4],
                mdct_enc[band][4], mdct_enc[band - 1][17 - 4], MDCT_CS4,
                MDCT_CA4);
          cmuls(mdct_enc[band][5], mdct_enc[band - 1][17 - 5],
                mdct_enc[band][5], mdct_enc[band - 1][17 - 5], MDCT_CS5,
                MDCT_CA5);
          cmuls(mdct_enc[band][6], mdct_enc[band - 1][17 - 6],
                mdct_enc[band][6], mdct_enc[band - 1][17 - 6], MDCT_CS6,
                MDCT_CA6);
          cmuls(mdct_enc[band][7], mdct_enc[band - 1][17 - 7],
                mdct_enc[band][7], mdct_enc[band - 1][17 - 7], MDCT_CS7,
                MDCT_CA7);
        }
      }
    }

    /* Save latest granule's subband samples to be used in the next mdct call */
    memcpy(config->l3_sb_sample[ch][0],
           config->l3_sb_sample[ch][config->mpeg.granules_per_frame],
           sizeof(config->l3_sb_sample[0][0]));
  }
}
