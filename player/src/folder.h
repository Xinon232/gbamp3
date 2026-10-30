/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Streaming directory listing under 0:/gbamp3.
 * No per-folder limit: entries are re-read on demand from checkpoints. */
#pragma once
#include "ff.h"
#include "storage.h"
#define FOLDER_MARKS 32
#define FOLDER_STRIDE 32   /* initial; doubles when the marks fill up */
enum {FOLDER_DIRS=0,FOLDER_SONGS=1};
/* A checkpoint: only the read position of `dir` (16 bytes, not a whole DIR:
 * 32 of them per cursor, and an exFAT DIR is 80 bytes). */
struct FolderMark {DWORD dptr,clust;LBA_t sect;BYTE *at;};
struct DirCursor {
 char path[STORAGE_PATH];
 unsigned kind,counted,count,pos,open,marks,stride;
 DIR dir;
 struct FolderMark mark[FOLDER_MARKS];
 /* Optional alphabetical order (folded-name records in borrowed scratch). */
 uint8_t *sort;unsigned sort_cap,sort_rec,sort_state,sort_epoch;
};
/* Scratch shared with Update Library; bumping this invalidates every order. */
extern unsigned folder_sort_epoch,folder_sort_busy;
/* Lend `cap` bytes of scratch so entries are listed alphabetically (folded,
 * accent-insensitive). Without it, or if a folder is too large, the listing
 * falls back to directory order. Call before folder_bind. */
void folder_sort_area(struct DirCursor *c, uint8_t *buf, unsigned cap);
/* Service hook called during long directory loops (keeps audio fed). */
extern void (*folder_yield)(void);
/* Bind a cursor to a folder path ("0:/gbamp3" or below) and entry kind. */
void folder_bind(struct DirCursor *c, const char *path, unsigned kind);
/* Number of entries of the cursor's kind (counts on first use). -1 on error. */
int folder_count(struct DirCursor *c);
/* v1.5: 1 when folder_name() returns names in folded alphabetical order. */
int folder_sorted(struct DirCursor *c);
/* Name of entry `index` in listing order (UTF-8). Returns 0, or -1 on error/out of range. */
int folder_name(struct DirCursor *c, unsigned index, char *name, unsigned cap);
/* Full path of child `name`; 0 if it would exceed STORAGE_PATH. */
int folder_join(char *out, const char *dir, const char *name);
int folder_is_song(const char *name);   /* .mp3 / .gac, as Show Songs allows */
extern unsigned folder_song_mask;       /* v1.8: bit 0 MP3, bit 1 GAC (Folders and Update Library) */
/* The data folder is never listed or scanned (matched by name, any case). */
#define FOLDER_SYSTEM "GBAMP3.SYS"
int folder_system(const char *name);
