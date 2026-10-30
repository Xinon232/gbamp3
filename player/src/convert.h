/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Converting songs on the GBA.
 * MP3 -> GAC v4 (32768 Hz, stereo layout): Helix decodes every band
 * (output mode 3), a 16-tap polyphase filter resamples to 32768 Hz,
 * gac_enc.c encodes. The blocks and their start frames go to NAME.blk and
 * NAME.sk first; the file is then assembled as NAME.part (header, metadata,
 * cover, seek table, blocks), read back and checked, and renamed NAME. A
 * cancelled or failed conversion leaves no file behind (except an existing
 * NAME, which is never touched: the rename fails instead). */
#pragma once
#include <stdint.h>
#include "meta.h"

/* Called after every decoded MP3 frame and while the file is assembled:
 * `done` of about `total` units (output frames; assembly adds the blocks).
 * A nonzero return cancels. */
typedef int (*convert_poll_fn)(void *ctx, uint32_t done, uint32_t total);
enum {CONVERT_OK=0,CONVERT_READ=-1,CONVERT_CANCELLED=-2,CONVERT_NO_AUDIO=-3,CONVERT_WRITE=-4,CONVERT_VERIFY=-5,CONVERT_EXISTS=-6};
#define CONVERT_WORK_BYTES 51200u
/* src: the MP3; dst: the GAC to create. meta: the song's tags (title,
 * artist, album, album artist, genre, track, disc, duration for progress).
 * art: 64x64 RGB555 cover or 0. work: CONVERT_WORK_BYTES, word aligned.
 * The Helix decoder must be loaded with a fresh arena (the caller's job);
 * writes must be enabled. Returns CONVERT_*. */
/* use(0) loads the MP3 decoder into IWRAM, use(1) the resampler and
 * encoder (conversion overlay); 0 on the host. inL/inR: cap frames (at least
 * 1152 + 16) holding decoded audio between the two. */
typedef void (*convert_use_fn)(int conversion);
int convert_mp3_to_gac(const char *src, const char *dst, const struct SongMeta *meta, const uint16_t *art,
                       uint8_t *work, int16_t *inL, int16_t *inR, unsigned cap, convert_use_fn use,
                       convert_poll_fn poll, void *ctx);
/* GAC -> MP3 (v2.0): the GAC decoder (16-bit output), the resampler to
 * 32000 Hz (RS_DOWN), shine (player/vendor/shine, LGPL v2) at
 * CONVERT_MP3_KBPS joint stereo. The tag (ID3v2.4: song fields from the
 * GAC metadata, its cover as a JPEG) and the frames go to NAME.part, which
 * is read back and checked, then renamed NAME. work: CONVERT_GAC_WORK_BYTES,
 * word aligned. use(0) loads the GAC decoder (keeping its state), use(1) the
 * conversion overlay. inL/inR: cap frames (at least 1024). Progress: input
 * frames of the GAC. Returns CONVERT_*. */
#define CONVERT_GAC_WORK_BYTES 65536u
#define CONVERT_MP3_KBPS 128
int convert_gac_to_mp3(const char *src, const char *dst, uint8_t *work, int16_t *inL, int16_t *inR, unsigned cap,
                       convert_use_fn use, convert_poll_fn poll, void *ctx);
/* Statistics of the last conversion (cycles need a timer: the caller's). */
struct ConvertStats {uint32_t frames_in, frames_out, blocks, bad_frames, rate, cyc_decode, cyc_resample, cyc_encode, cyc_lpc, cyc_sub, cyc_close, peak_dec, peak_res;};   /* cycles: GBA only */
extern struct ConvertStats convert_stats;
