/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Song metadata for MP3 (ID3) and GAC files. */
#pragma once
#include <stdint.h>
#include "ff.h"
#define META_TITLE 128
#define META_NAME 96
#define META_GENRE 64
enum {FORMAT_MP3=1,FORMAT_GAC=2};
struct SongMeta {
 char title[META_TITLE],artist[META_NAME],album[META_NAME],album_artist[META_NAME],genre[META_GENRE];
 uint16_t track,disc;
 uint32_t duration_ms;   /* 0: unknown */
 uint32_t rate;          /* source samples per second */
 uint8_t format,channels,has_art,title_from_name;
 uint16_t bitrate;       /* MP3 kbps when constant (0: unknown) */
 uint8_t vbr,pad;        /* MP3: variable bitrate (Xing/VBRI header) */
 uint32_t pic_off,pic_len;   /* MP3: embedded JPEG front cover in the file (0: none) */
};
/* Fill from an open file. Missing title falls back to the filename without
 * its extension (title_from_name=1); other missing fields stay empty and are
 * shown as Unknown Artist/Album/Genre. Returns 0, or -1 on I/O error. The file
 * position is left undefined. `name` is the filename (last path element). */
int meta_read(FIL *f, const char *name, struct SongMeta *m);
/* ID3v2 APIC (v2.3/2.4) / PIC (v2.2) frame payload `p` (the first `n` of
 * `size` bytes; the frame starts at file offset `at`): JPEG picture type and
 * data position. Returns the picture type (0..255), or -1 if it is not a
 * usable JPEG (other format, description not within `n`, larger than
 * JPEG_MAX_BYTES). */
int meta_id3_picture(const unsigned char *p,unsigned n,unsigned size,unsigned v22,uint32_t at,uint32_t *off,uint32_t *len);
/* GAC metadata block: GACMETA3 (UTF-8, genre, disc). */
int meta_gac_block(const unsigned char block[512], struct SongMeta *m);
/* ID3v2 text-frame payload (encoding byte + text) -> UTF-8, first string only. */
void meta_id3_text(const unsigned char *p, unsigned n, char *out, unsigned cap);
const char *meta_genre_name(unsigned id3v1_genre);
