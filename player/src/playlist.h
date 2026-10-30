/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). On-device playlists (no count limits).
 * Directory: PLIST0/1.DAT (two slots). Contents: Pxxxxxxxx.DAT, one 512-byte
 * sector per entry so in-place edits write whole sectors. */
#pragma once
#include <stdint.h>
#include "meta.h"
#include "storage.h"
#define PLAYLIST_NAME 64
struct PlaylistEntry {
 char path[STORAGE_PATH];char title[META_TITLE];char artist[META_NAME];
 uint32_t duration_ms;int valid;
};
int playlist_init(void);
unsigned playlist_count(void);
int playlist_info(unsigned index,uint32_t *id,char *name);
/* Index of a playlist whose name folds equal to `name`, or -1. */
int playlist_find_name(const char *name);
/* 0 ok, -2 duplicate name, -3 empty name, -1 I/O. New playlists go last. */
int playlist_create(const char *name,uint32_t *id);
int playlist_rename(unsigned index,const char *name);
int playlist_delete(unsigned index);
int playlist_move(unsigned from,unsigned to);
unsigned playlist_length(uint32_t id);
int playlist_entry(uint32_t id,unsigned i,struct PlaylistEntry *e);
/* v1.6: the same without checking that the song file exists (valid = 1);
 * the list screen checks visible rows afterwards with playlist_exists(). */
int playlist_entry_quick(uint32_t id,unsigned i,struct PlaylistEntry *e);
int playlist_exists(const char *path);
int playlist_add(uint32_t id,const char *path,const char *title,const char *artist,uint32_t duration_ms);
int playlist_remove(uint32_t id,unsigned i);
int playlist_swap(uint32_t id,unsigned i,unsigned j);
/* v1.9: entries of a renamed file (dir 0) or of the songs in a renamed
 * folder (dir 1) take the new path. */
int playlist_rename_paths(const char *old,const char *new,unsigned dir);
/* 11.0 Update Library: removes the entries whose song file is absent
 * (never on a read error: -1, the rest kept); known(path) 1: in the
 * library (kept, no card search). on_removed(id, at): each
 * removal in order, `at` its index at that moment. */
int playlist_prune(uint32_t id,int (*known)(const char *path),void (*on_removed)(uint32_t id,uint32_t at),unsigned *removed);
