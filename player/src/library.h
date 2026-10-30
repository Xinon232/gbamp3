/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GBA-built music library (index v5), no song cap.
 * Files live in 0:/gbamp3/GBAMP3.SYS; two slots, header written last. */
#pragma once
#include <stdint.h>
#include "meta.h"
#include "storage.h"
enum {PERM_TITLE,PERM_ARTIST,PERM_ALBUM,PERM_GENRE,PERM_FILE,PERM_PATH,PERM_COUNT};
enum {GROUP_ARTISTS,GROUP_ARTIST_ALBUMS,GROUP_ALBUMS,GROUP_GENRES,GROUP_COUNT};
enum {FIELD_TITLE,FIELD_FILENAME,FIELD_ALBUM,FIELD_ARTIST};
#define LIBRARY_DIR "0:/gbamp3/GBAMP3.SYS"
struct LibSong {
 uint32_t id,size,date,duration_ms,rate;
 uint16_t track,disc;
 uint8_t format,channels,has_art,title_from_name,vbr;
 uint16_t bitrate;
 uint32_t pic_off,pic_len;             /* MP3 embedded JPEG (0: none) */
 char path[STORAGE_PATH];              /* full, "0:/gbamp3/..." */
 char title[META_TITLE],artist[META_NAME],album[META_NAME],album_artist[META_NAME],genre[META_GENRE];
};
/* A contiguous run of one permutation; ARTISTS also point at their slice of
 * ARTIST_ALBUMS (sub_start/sub_count). `rep` is a record whose field names it. */
struct LibGroup {uint32_t start,count,rep,sub_start,sub_count;};
int library_init(void);
/* Close the index before removing only LIB0.IDX and LIB1.IDX. */
int library_delete_index(void);
unsigned library_count(void);
unsigned library_generation(void);
int library_song(uint32_t id,struct LibSong *s);
int library_perm(unsigned perm,uint32_t pos,uint32_t *id);
unsigned library_groups(unsigned kind);
int library_group(unsigned kind,uint32_t index,struct LibGroup *g);
/* Songs whose `field` starts with `query` (folded): [*lo,*hi) of the field's
 * permutation, returned in *perm. Empty query: the whole title order. */
int library_prefix(unsigned field,const char *query,unsigned *perm,uint32_t *lo,uint32_t *hi);
/* v1.2 title word search. Nonzero when the library has a word index (a v4
 * library from v0.9.8-v1.1 has none until the next Update Library). */
unsigned library_words(void);
/* Word entries [*lo,*hi) whose title word starts with `query` (folded,
 * *qb bytes); *count songs, each once. -1 without word index or on error. */
int library_word_range(const char *query,uint32_t *lo,uint32_t *hi,unsigned *qb,uint32_t *count);
/* Record id of result i of such a range. */
int library_word_song(uint32_t lo,uint32_t hi,unsigned qb,uint32_t i,uint32_t *id);
/* Record id of a full path, or -1. */
int library_find(const char *path,uint32_t *id);
/* v0.9.8: the album cover of song `id` (MP3 songs of an album with a usable
 * embedded JPEG), 64x64 RGB555 values. 0, or -1 when it has none. */
int library_cover(uint32_t id,volatile uint16_t *out);   /* v1.8: 64x64 (a v4/v5 index's 32x32 doubled); out may be VRAM */
extern unsigned library_cover_decodes;   /* pictures decoded by updates (tests, telemetry) */
/* Progress callback: phase 0 scanning (done = songs), 1..6 sorting, 7 album
 * covers (done = albums), 8 title word index (v1.2). Nonzero cancels. */
typedef int (*LibraryProgress)(unsigned phase,unsigned done);
/* Full rescan of /gbamp3. Unchanged files reuse their stored tags. Returns 0,
 * -1 on error (old library kept), -2 cancelled (old library kept). */
int library_update(LibraryProgress progress);
/* The update's 64 KB sort buffer, lent out as folder-order scratch between updates. */
uint8_t *library_scratch(unsigned *bytes);
/* Create the hidden data folder if missing (writes enabled by caller). */
int library_system_dir(void);
/* v2.2 exFAT: repair entry sets torn by a power cut (exfat_repair.h) in `dir`
 * and the folders above it / in the folder holding `path` (writes enabled
 * by caller). >0 repairs made, 0 nothing to do (always on FAT), <0 error. */
int library_repair_dir(const char *dir);
int library_repair_folder_of(const char *path);
/* f_open, retried once after a repair when it fails with FR_INT_ERR. */
FRESULT library_open(FIL *f,const char *path,BYTE mode);
/* Display fallbacks. */
const char *library_artist(const char *s);
const char *library_album(const char *s);
const char *library_genre(const char *s);
