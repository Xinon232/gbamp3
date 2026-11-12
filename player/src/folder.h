/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Streaming directory listing under 0:/gbamp3.
 * No per-folder limit. 12.0: a folder is read once when it is opened (the
 * cursors bound to it are filled in the same pass); every entry's place in
 * the folder is kept, so a row is read straight from the card. */
#pragma once
#include "ff.h"
#include "storage.h"
#define FOLDER_MARKS 32
#define FOLDER_STRIDE 32     /* initial; doubles when the marks fill up */
#define FOLDER_CLUSTERS 256  /* 1 MiB of folder with 4 KiB clusters; then every 2nd, 4th ... cluster */
#define FOLDER_CURSORS 4
enum {FOLDER_DIRS=0,FOLDER_SONGS=1};
/* A place in the folder is an entry number (read offset / 32): the offset
 * f_readdir started from when it returned the entry. The cluster that
 * holds it comes from `clus`, the sector from the cluster. */
struct DirCursor {
 char path[STORAGE_PATH];
 unsigned kind,counted,count,pos,open,marks,stride;
 DIR dir;
 uint32_t mark[FOLDER_MARKS];      /* place of entry (k+1)*stride of this kind: used when the scratch is not */
 uint32_t clus[FOLDER_CLUSTERS];   /* cluster number of the folder's cluster k<<clus_shift (0: not seen) */
 unsigned clus_n,clus_shift;
 /* Borrowed scratch: sort_state 1: folded-name records in alphabetical
  * order, each ending in its place (16 bits); 3: the places alone, in card
  * order (every tab_stride-th entry), when the records do not fit;
  * 2: unused (places over 16 bits); 0: not filled. */
 uint8_t *sort;unsigned sort_cap,sort_rec,rec_max,sort_state,sort_epoch,tier,tab_stride;
 unsigned gen,loading;
};
/* Scratch shared with Update Library; bumping this invalidates every order. */
extern unsigned folder_sort_epoch,folder_sort_busy;
/* Lend `cap` bytes of scratch so entries are listed alphabetically (folded,
 * accent-insensitive). Without it, or if a folder is too large, the listing
 * falls back to directory order. Call before folder_bind. */
void folder_sort_area(struct DirCursor *c, uint8_t *buf, unsigned cap);
/* Service hook called during long directory loops (keeps audio fed): before
 * every entry read and every few steps of the sort. */
extern void (*folder_yield)(void);
/* Bind a cursor to a folder path ("0:/gbamp3" or below) and entry kind. */
void folder_bind(struct DirCursor *c, const char *path, unsigned kind);
/* Number of entries of the cursor's kind (reads the folder on first use). -1 on error. */
int folder_count(struct DirCursor *c);
/* v1.5: 1 when folder_name() returns names in folded alphabetical order. */
int folder_sorted(struct DirCursor *c);
/* Name of entry `index` in listing order (UTF-8). Returns 0, or -1 on error/out of range. */
int folder_name(struct DirCursor *c, unsigned index, char *name, unsigned cap);
/* 12.0: read the folder now if the cursor needs it (outside the decoder). */
void folder_prepare(struct DirCursor *c);
/* 12.0: entries of `kind` in `path`, counted without a cursor. -1 on error. */
int folder_count_path(const char *path, unsigned kind);
/* 12.0: a file or folder was renamed, deleted or made: every cursor reads its folder again. */
void folder_changed(void);
/* 12.0: `path` is the song folder_name(c, ...) just returned: remember its
 * place. folder_open() opens a song for reading, the remembered one without
 * looking its name up in its folder. */
void folder_keep(struct DirCursor *c, const char *path);
/* 12.0: entry `index`'s folded sort key (UTF-8) without a card read; -1: not kept. */
int folder_key(struct DirCursor *c, unsigned index, char *out, unsigned cap);
FRESULT folder_open(FIL *fp, const char *path);
/* Full path of child `name`; 0 if it would exceed STORAGE_PATH. */
int folder_join(char *out, const char *dir, const char *name);
int folder_is_song(const char *name);   /* .mp3 / .gac, as Show Songs allows */
extern unsigned folder_song_mask;       /* v1.8: bit 0 MP3, bit 1 GAC (Folders and Update Library) */
/* The data folder is never listed or scanned (matched by name, any case). */
#define FOLDER_SYSTEM "GBAMP3.SYS"
int folder_system(const char *name);
