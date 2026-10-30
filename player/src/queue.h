/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Playback queue: source list + position + modes. */
#pragma once
#include <stdint.h>
#include "storage.h"
/* QUEUE_WORDS (v1.2): Song Title word search results. The saved layout is
 * unchanged: perm = folded query bytes, start/playlist = word entry range
 * [start, playlist), count = results, generation as for QUEUE_LIBRARY. */
enum {QUEUE_NONE,QUEUE_LIBRARY,QUEUE_FOLDER,QUEUE_PLAYLIST,QUEUE_SINGLE,QUEUE_WORDS};
enum {MODE_NORMAL,MODE_REPEAT_ALL,MODE_REPEAT_ONE,MODE_SHUFFLE,MODE_COUNT};
#define QUEUE_OVERLAY 32
struct QueueSource {
 uint8_t kind,perm,pad[2];
 uint32_t start,count;      /* library: permutation range */
 uint32_t playlist;         /* playlist id */
 uint32_t generation;       /* library generation of a range */
 char text[STORAGE_PATH];   /* folder path, or the single song path */
};
extern unsigned queue_mode;
/* Start `src` at base index `index` (becomes current). */
void queue_set(const struct QueueSource *src,uint32_t index);
const struct QueueSource *queue_source(void);
uint32_t queue_count(void);
uint32_t queue_index(void);       /* base index of the current (or last base) item */
int queue_on_overlay(void);       /* current item came from Play Next */
int queue_path(uint32_t index,char *out);
int queue_current(char *out);     /* path of the current item */
/* Advance. manual: user RIGHT (NORMAL/SHUFFLE wrap to a new start).
 * Returns 1 with the new current path, 0 at the natural end (stopped). */
int queue_next(int manual,char *out);
/* Previous item (caller already decided not to restart). Returns 1, or 0 when
 * the current item stays (first item in NORMAL / pass start in SHUFFLE). */
int queue_prev(char *out);
/* After a natural end: start again (NORMAL/REPEAT: first item; SHUFFLE: new pass). */
int queue_restart(char *out);
int queue_play_next(const char *path);
void queue_set_mode(unsigned mode);
/* Playlist edits while its queue plays: removal/move of base entries. */
void queue_playlist_removed(uint32_t playlist,uint32_t index);
void queue_playlist_moved(uint32_t playlist,uint32_t from,uint32_t to);
/* Play Next list for saving/restoring it (v0.9.5, state.c pn_save/pn_load):
 * changed is set whenever the list or the on-overlay state changes. */
unsigned queue_pn_changed(void);
void queue_pn_saved(void);
unsigned queue_overlay_count(void);
char *queue_overlay_entry(unsigned i);   /* i < QUEUE_OVERLAY, writable for import */
char *queue_overlay_current(void);       /* path of the playing Play Next song */
/* After queue_set(): n imported entries; on: the current song came from Play Next. */
void queue_overlay_import(unsigned n,unsigned on);
void queue_pn_touch(void);   /* v1.9: entries edited in place (renamed files): save the list */
/* Resolver for library/folder/playlist entries (set by main). */
extern int (*queue_resolve)(const struct QueueSource *src,uint32_t index,char *out);
