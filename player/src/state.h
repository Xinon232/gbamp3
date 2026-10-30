/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Persistent application state (STATE0/1.DAT). */
#pragma once
#include <stdint.h>
#include "queue.h"
#include "menu_order.h"
struct AppState {
 uint8_t stereo,search_field,mode,has_song;
 char last_song[STORAGE_PATH];
 struct QueueSource queue;
 uint32_t queue_index;
 uint8_t boost_on,boost_level;          /* v0.9.5 Volume Boost: switch, level 0..3 */
 uint8_t auto_update;                   /* v1.5: Update Library at every start (was padding: 0) */
 uint8_t song_files;                    /* v1.6: Show songs as: 0 Titles, 1 File Names (was padding: 0) */
 struct MenuOrder menus;                 /* v1.3: independent stable-ID menu orders */
 /* v1.8 Secret Settings, in what was struct padding (0 in older files):
  * Show Songs 0 MP3 + GAC, 1 only MP3, 2 only GAC; + New Playlist on top;
  * letter box off. */
 uint8_t song_types,new_playlist_top,flags;   /* new_playlist_top: bit 0 New Playlist on top, v2.2: bit 1 Hold START to unlock Off (clear, the default and in older files: On), 11.0: bit 2 + Add Songs on top of playlists; flags: bit 0 letter box off, v1.9: bit 1 Rename files on, bit 2 Delete files on; bit 7 (11.0) Hold START to unlock was reset to On once (v2.2 test build: Hold START to unlock on; cleared on loading by 10.0) */
};
#define STATE_HOLD_RESET_DONE 0x80u
/* Defaults on missing/corrupt files. Returns 1 when a saved state was read. */
int state_load(struct AppState *s);
/* Two-slot write: the newer copy is only replaced after the other is committed. */
int state_save(const struct AppState *s);
/* Removes exactly the two STATE and two QUEUE slots; missing files count as
 * absent, any other unlink error is failure. Caller must first stop stale
 * in-memory writes, and must not report success if this returns nonzero. */
int state_delete_configuration(void);
/* Play Next list (v0.9.5): QUEUE0/1.DAT, two slots like the state file.
 * "GBAPN001", u32 generation, u32 flags (bit 0: the current song came from
 * Play Next), u32 count (0..32), the current path (NUL-terminated, only with
 * bit 0), count NUL-terminated paths in play order, FNV-1a32 of all before.
 * Streams straight from/to the queue's own arrays. */
int pn_save(void);
/* Remove both persisted Play Next slots without changing settings or playlists.
 * The caller clears the in-memory queue before invoking this. */
int pn_delete(void);
/* Loads the newer valid slot into the queue arrays (after queue_set):
 * returns the entry count (0 on missing/corrupt files), *on = flag bit 0. */
unsigned pn_load(unsigned *on);
