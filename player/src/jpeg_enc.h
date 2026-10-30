/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). A 64x64 cover as a baseline JPEG for MP3 tags
 * (GAC -> MP3 conversion): JPEG_FLAT_SIDE square, YCbCr 4:4:4, every 8x8
 * block flat (DC only, quantizer 1), so jpeg_dc.c (and any decoder) gives
 * back the 64x64 picture. */
#pragma once
#include <stdint.h>
#define JPEG_FLAT_SIDE 512u
#define JPEG_FLAT_MAX 40960u   /* room for any picture but noise (12288 DC codes) */
/* Returns the JPEG's size, or 0 if it does not fit in cap bytes. */
unsigned jpeg_flat_cover(const uint16_t *rgb555, uint8_t *out, unsigned cap);
