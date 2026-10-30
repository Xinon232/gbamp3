/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Baseline JPEG -> 64x64 RGB555 cover (v0.9.8: 32x32).
 * 1/4-scale decode (v1.8): every block is Huffman-decoded; from its DC
 * coefficient and the AC coefficients of horizontal and vertical frequencies
 * 1, 3, 5, 7 the means of its four 4x4 quarters are computed exactly (the
 * other frequencies average to zero over a half block). The quarter means
 * are box-filtered by area into a grid that fits the picture into 64x64
 * (aspect kept, centred, black border), as the converter's covers. No full
 * IDCT, no full-size image.
 * tools/jpeg_dc.py is the reference; tests/test_jpeg_dc.py compares them. */
#pragma once
#include <stdint.h>
#define JPEG_MAX_BYTES (256u*1024u)   /* larger picture frames are skipped */
#define JPEG_MAX_SIDE 1600u           /* wider or taller pictures are skipped */
#define JPEG_MAX_SECONDS 5u           /* per picture during Update Library */
#define JPEG_SIDE 64u                 /* v1.8: covers are 64x64 */
#define JPEG_WORK_BYTES 49152u        /* caller's work area (static, not stack): the 64x64 sums */
/* Reads up to cap bytes of the picture from offset `at`; returns the count
 * read (0 at the end) or -1 on error. */
typedef int (*JpegRead)(void *ctx,uint32_t at,uint8_t *buf,unsigned cap);
/* Nonzero: stop (time limit). Called once per MCU row. May be 0. */
typedef int (*JpegStop)(void *ctx);
enum {JPEG_OK=0,JPEG_ERR_IO=-1,JPEG_ERR_FORMAT=-2,JPEG_ERR_UNSUPPORTED=-3,JPEG_ERR_SIZE=-4,JPEG_ERR_TIME=-5,JPEG_ERR_DATA=-6};
/* Decodes `bytes` bytes of JPEG into out[64*64] (little-endian RGB555
 * values, row-major). Never reads outside [0, bytes) nor writes outside
 * work/out. */
int jpeg_dc_cover(JpegRead read,JpegStop stop,void *ctx,uint32_t bytes,void *work,uint16_t out[JPEG_SIDE*JPEG_SIDE]);
/* v2.1: a 64x64 RGB555 picture reduced to at most COVER_COLORS colours as a
 * cover record (cover.h; median cut, tests/cover_quant.py is the same).
 * work: the box table (2 KB). On the GBA it loads the cover
 * overlay and reuses the decoder's IWRAM state (after a decode). */
void jpeg_cover_reduce(const uint16_t *img,uint8_t *rec,void *work);
