/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v1.8 album covers: 64x64, kept in two slots of
 * video memory that no screen shows (see storage.c). VRAM takes no byte
 * writes, so covers are copied in halfwords.
 * v2.1: every screen is mode 4 (8-bit palette pixels). The library holds each
 * cover reduced to at most COVER_COLORS colours (median cut at Update
 * Library): COVER_REC bytes, the 64x64 colour numbers, then COVER_COLORS
 * RGB555 colours (unused ones 0) and padding. GAC files keep RGB555 covers
 * (COVER_BYTES_64 / _32); Update Library reduces those too. */
#pragma once
#include <stdint.h>
#define COVER_SIDE 64
#define COVER_PIXELS (COVER_SIDE*COVER_SIDE)
#define COVER_BYTES_64 8192u
#define COVER_BYTES_32 2048u   /* 32x32 covers of older GAC files: shown doubled */
#define COVER_COLORS 240
#define COVER_PAL COVER_PIXELS        /* palette offset in a record */
#define COVER_REC 4608u               /* 4096 + 240 colours, padded to 512 */
/* Little-endian RGB555 bytes -> halfwords. */
static inline void cover_copy16(volatile uint16_t *dst,const unsigned char *src,unsigned bytes){
 for(unsigned i=0;i<bytes/2;i++)dst[i]=(uint16_t)(src[2*i]|src[2*i+1]<<8);
}
/* A 32x32 cover at the start of img -> 64x64 in place (from the end, so no
 * source pixel is overwritten before it is read). */
static inline void cover_double(volatile uint16_t *img){
 for(int y=COVER_SIDE-1;y>=0;y--)for(int x=COVER_SIDE-1;x>=0;x--)img[y*COVER_SIDE+x]=img[(y>>1)*32+(x>>1)];
}
