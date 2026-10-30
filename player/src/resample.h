/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Resampling to 32768 Hz for converting songs
 * (convert.c): polyphase windowed-sinc, 16 taps, 256 phases
 * (resample_tables.h). Runs from the conversion overlay (IWRAM). Also
 * 32768 -> 32000 Hz for MP3 output (resample_begin_down). */
#pragma once
#include <stdint.h>
struct Resampler {uint32_t base,frac,rate;const int16_t (*tab)[16];uint32_t num,den,rem;};   /* step = rate + num/den (Q15) */
void resample_begin(struct Resampler *r, unsigned rate);
/* 32768 -> 32000 Hz (GAC -> MP3): input frames per output frame in Q15,
 * 32768 / 32000 = 33554 + 54/125 exactly. */
#define RS_DOWN_STEP 33554u
void resample_begin_down(struct Resampler *r);
/* Input frames in[0..avail) (L and R arrays; frame base-7 .. base+8 are the
 * taps of the next output frame, so the first call needs 7 zero frames in
 * front). Writes at most max interleaved output frames; with flush, frames
 * up to the input end are made (taps past it count as zero). Returns the
 * number of frames written. The caller then drops input before base-7. */
unsigned resample_run(struct Resampler *r, const int16_t *L, const int16_t *R, uint32_t avail, int flush, int16_t *out, unsigned max);
