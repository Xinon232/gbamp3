/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4 explicit little-endian parser and decoder. */
#pragma once
#include <stdint.h>
#define GAC_BLOCK 1024u
#define GAC_UNIT 64u
#define GAC_MAX_FRAMES 8192u
#define GAC_HEADER 64u
/* audio: offset of block 0; seek: offset of the seek table (u32 per block). */
/* art: offset of the cover (0: none), art_bytes its size: 2048 (32x32) or,
 * from v1.8, 8192 (64x64). */
struct GacInfo {uint32_t rate,total,blocks,channels,seek,audio,meta,art,art_bytes;};
/* Output ring cursor and per-channel noise-shaping error state. */
/* gain: Volume Boost, Q8 (256 = 0 dB, the unchanged output path). */
struct GacOut {int8_t *l,*r;uint8_t *tag;unsigned w,mask,tagv,mono;int el,er,gain;};
/* 1: valid v4 header; 0: invalid; -1: an older GAC version (v1..v3). */
int gac_header(const unsigned char *,unsigned bytes,unsigned file_size,struct GacInfo *);
int gac_metadata_valid(const unsigned char *h,const unsigned char *meta);
int gac_cover_valid(const unsigned char *h,const unsigned char *art);
/* FNV-1a32 over n more bytes (a cover read in pieces: header byte 56). */
unsigned gac_fnv(unsigned hash,const unsigned char *p,unsigned n);
/* Seek table: first frame of every block, 0 first, strictly increasing,
 * at most GAC_MAX_FRAMES apart and before total; FNV-1a32 in header byte 60.
 * Checked in pieces: gac_seek_check(0,..) for the first piece. */
struct GacSeekCheck {uint32_t hash,prev,count;};
int gac_seek_check(struct GacSeekCheck *,const unsigned char *entries,unsigned n,const struct GacInfo *);
int gac_seek_done(const struct GacSeekCheck *,const unsigned char *h,const struct GacInfo *);
/* Frames stored in a block (header), clamped to 1..GAC_MAX_FRAMES. */
unsigned gac_block_frames(const unsigned char *block);
/* Block decoder. The state lives in IWRAM scratch inside the GAC overlay. */
struct Gac4Stream {int32_t c[8];unsigned order,idx;};
struct Gac4 {
 uint32_t block[GAC_BLOCK/4];          /* copy of the current block */
 const uint16_t *p,*end;uint32_t acc;unsigned cnt;
 unsigned channels,side,skip;int prev;
 struct Gac4Stream m,s;
 int32_t mx[8+64],sx[8+32];            /* history (8) + one unit of samples */
 int q[32];
};
extern struct Gac4 gac4;
/* Start decoding gac4.block (already copied in). skip: output frames of the
 * block to decode but not emit (seek inside a block). */
void gac4_begin(unsigned channels,unsigned skip);
/* Decode up to `units` more units into the ring; returns frames emitted.
 * Every byte pattern decodes; reads never leave the block. */
unsigned gac4_decode(unsigned units,unsigned frames_left,struct GacOut *);
/* v2.0: decode up to `units` units as 16-bit L and R (conversion). */
unsigned gac4_decode16(unsigned units,unsigned frames_left,int16_t *L,int16_t *R);
