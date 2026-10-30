#pragma once
/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Song file input by path under 0:/gbamp3. */
#include "mp3_header.h"
#include "gac4.h"
#include "ff.h"
#define STORAGE_PATH 256
unsigned storage_is_cache(void);
unsigned storage_path_is_cache(const char *path);
const struct GacInfo *storage_cache_info(void);
int storage_cache_next(unsigned char **data,unsigned *frames);
int storage_cache_seek(unsigned seconds,unsigned *sample);
/* Position at the block holding frame `frame` (clamped to the last frame). */
int storage_cache_seek_frame(unsigned frame,unsigned *sample);
int storage_init(void);
FATFS *storage_fs(void);   /* v2.2: the mounted volume (exFAT repair) */
/* Stable identity of a path (covers, seek points, UI). Never zero. */
unsigned storage_key(const char *path);
const volatile unsigned short*storage_cover(unsigned key);   /* v1.8: 64x64, in video memory */
/* v0.9.8 MP3 covers: the next of the two cover slots, assigned to `key` but
 * not shown until storage_cover_ready(key). */
volatile unsigned short *storage_cover_slot(unsigned key);
void storage_cover_ready(unsigned key);
int storage_open(const char *path);
void storage_close(void);   /* v1.9: before renaming or deleting files */
FIL *storage_file(void);
int storage_next(unsigned char **data,unsigned *bytes);
unsigned storage_position(void);
unsigned storage_size(void);
int storage_at(unsigned offset);
int storage_seek_seconds(unsigned seconds,unsigned *warm,unsigned *target,struct MP3Header *h);
/* 0 success/EOF, 1 frame, -1 I/O, -2 filesystem, -3 invalid file, -4 old GAC version. */
