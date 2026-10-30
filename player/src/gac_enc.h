/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4 encoder (stereo layout: mid + half-rate
 * side) for converting on the GBA. Integer port of tools/gac4.py encode():
 * per-block LPC from a look-ahead window, closed-loop rate-distortion choice
 * of the step size per 32-sample subblock among 7 candidates. Not bit-exact
 * with the Python encoder (that one analyses in floating point); its blocks
 * follow the same format and decode with the same decoder (GAC_FORMAT.md).
 * Streaming: push 32768 Hz stereo PCM; finished 1024-byte blocks come out
 * through the callback with the frame they start at. All state is in
 * struct GacEnc (about 17 KB), which the caller places, apart from 9 KB of
 * static work arrays (one encoder at a time). */
#pragma once
#include <stdint.h>

#define GE_MID_RING 4096u    /* mid samples kept (power of two) */
#define GE_SIDE_RING 2048u   /* decimated side samples kept */
#define GE_RAW_RING 64u      /* raw side samples for the lowpass */

typedef int (*gac_enc_block_fn)(void *ctx, const uint8_t block[1024], uint32_t first_frame);

struct GacEncStream {
 int16_t c[8];                /* Q12 coefficients of the open block (0 past the order) */
 int16_t h[8];                /* decoded history, newest first */
 int16_t hist[8];             /* source history stored in the block header */
 uint8_t refl[8],order,first,cur,init;
};
struct GacEncSub {int16_t q[32],x[32],newh[8];uint32_t bits;uint8_t idx,tab,cur;uint64_t d;};
struct GacEnc {
 int16_t mid[GE_MID_RING],side[GE_SIDE_RING],raw[GE_RAW_RING];
 struct GacEncStream m,s;
 uint32_t pushed;             /* frames pushed (zeros after finish included) */
 uint32_t total,units;        /* real frames and units (set by finish) */
 uint32_t side_made;          /* decimated side samples computed */
 uint32_t unit;               /* next unit to code */
 uint32_t block_start,block_units,block_bits,block_cap;
 int block_open,finishing;
 int32_t side_peak;           /* largest |side| in the open block (silent side) */
 uint32_t side_live;          /* a side subblock not zero in the open block */
 gac_enc_block_fn out;void *ctx;
 uint32_t blocks;
 uint64_t sq_err;             /* squared error of the coded mid and side samples (statistics) */
 uint16_t side_at[128];       /* bit position of each unit's side code (silent side: removed) */
 uint8_t bitbuf[1024],block[1024];
 uint32_t inv_step[64];      /* round(2^24 / step) */
 uint32_t cyc_lpc,cyc_sub,cyc_close,evals,cyc_l[3];   /* GBA timer cycles in lpc(), code_sub(), close_block() (statistics) */
};
void gac_enc_begin(struct GacEnc *e, gac_enc_block_fn out, void *ctx);
/* Frames of interleaved L,R signed 16-bit. Returns 0, or the callback's error. */
int gac_enc_push(struct GacEnc *e, const int16_t *lr, unsigned frames);
/* Codes the rest; the total is the number of frames pushed (at least 1).
 * Returns 0, -1 without frames, or the callback's error. */
int gac_enc_finish(struct GacEnc *e);
