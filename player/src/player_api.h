/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Player operations used by the user interface. */
#pragma once
#include <stdint.h>
#include "queue.h"
#include "meta.h"
#include "library.h"
#include "menu_order.h"
struct NowPlaying {unsigned valid,key;char path[STORAGE_PATH];struct SongMeta meta;};
/* The audible track, or the loaded/last song while stopped (paused, 0:00). */
const struct NowPlaying *player_now(void);
unsigned player_elapsed(void);            /* seconds */
unsigned player_is_playing(void);
unsigned player_loaded(void);             /* a current song exists */
void player_play(const struct QueueSource *src,uint32_t index);
void player_toggle(void);
/* A track is loaded and not finished (playing or paused mid-song). */
unsigned player_active(void);
void player_pause(void);
void player_next(void);
void player_prev(void);
void player_seek(int seconds);
unsigned player_mode(void);
void player_set_mode(unsigned mode);
unsigned player_stereo(void);
void player_set_stereo(unsigned stereo);
unsigned player_search_field(void);
void player_set_search_field(unsigned field);
int player_play_next(const char *path);
unsigned player_output_hz(void);          /* actual FIFO output rate */
unsigned player_output_mono(void);
/* Volume Boost (v0.9.5): on/off switch (Settings) and level 0..3 = 0/+3/+6/+9 dB
 * (Now Playing Up/Down, only while on). Switching on starts at 0 dB. */
unsigned player_boost_on(void);
int player_scroll_ok(void);               /* v1.6: enough audio buffered for a staged list move */
int player_read_ahead_ok(void);           /* v1.7: 1/4 s buffered: a list may paint all rows in one frame */
unsigned player_boost_level(void);        /* effective: 0 while off */
void player_set_boost_on(unsigned on);
void player_boost_step(int dir);
unsigned player_fatal(void);
const char *player_error_text(void);
int player_update_library(LibraryProgress progress);
void player_service(void);                /* keep audio fed during long UI work */
void player_service_floor(void);          /* v1.8: the same, only while below 1/8 s buffered */
void player_fill(void);                   /* top the audio buffer up before SD writes */
const struct MenuOrder *player_menus(void);
void player_menu_toggle(void);
/* v1.5 Secret Settings: Update Library automatically at every start. */
unsigned player_auto_update(void);
void player_auto_update_toggle(void);
/* v1.8 Secret Settings: Show Songs 0 MP3 + GAC (default), 1 only MP3, 2 only
 * GAC; "+ New Playlist" at the top of Playlists (else the bottom); the
 * letter box of long lists on (default) or off. */
unsigned player_song_types(void);
void player_song_types_next(void);
unsigned player_new_playlist_top(void);
void player_set_new_playlist_top(unsigned top);
unsigned player_add_songs_top(void);        /* 11.0: "+ Add Songs" on top of playlists */
void player_set_add_songs_top(unsigned top);
unsigned player_letter_box(void);
void player_letter_box_toggle(void);
/* v1.9 Secret Settings: Rename files (bit 0) / Delete files (bit 1). */
unsigned player_file_ops(void);
void player_file_ops_toggle(unsigned bit);
unsigned player_conversion(void);          /* v2.0: 0 Off, 1 MP3>GAC, 2 GAC>MP3 */
void player_conversion_toggle(void);
/* v2.2 Secret Settings > Hold START to unlock: 1 On (default), 0 Off:
 * START unlocks only when held 1 s (lock screen and sleep). */
unsigned player_hold_unlock(void);
void player_hold_unlock_toggle(void);
unsigned player_ticks(void);               /* 16.78 MHz free-running counter (wraps after 256 s) */
int player_replace_original(const char *old,const char *new_path);   /* v2.0: after a conversion */
/* v2.0 conversion: keys down now or since the last call; the first call
 * starts sampling them 60 times a second (timer 0, music off), release
 * stops it. */
unsigned player_keys_take(void);
void player_keys_release(void);
/* v1.9 file operations: playback stops and the current song is forgotten
 * first. Rename fixes Play Next and playlists (dir: a folder); the library
 * follows with the next Update Library. 0 ok, -1 failed. */
void player_stop_all(void);
/* v1.9: lock screen with nothing playing: BIOS Stop until START. */
void player_sleep(void);
int player_rename(const char *old,const char *new_path,unsigned dir);
int player_delete_file(const char *path);
/* v1.9.1: set the artist (0), album (1) or genre (2) tag of a song file (see
 * tags.h; call player_stop_all first). 0, -1 write failure, -2 not possible. */
int player_tag_set(const char *path,unsigned field,const char *text);
/* Conversion: MP3 -> GAC (convert.h); stops the music; CONVERT_* result. */
#include "convert.h"
int player_convert(const char *src,const char *dst,convert_poll_fn poll,void *ctx);   /* MP3 -> GAC, or GAC -> MP3 for a .gac source */
int player_delete_folder(const char *dir,unsigned *deleted,unsigned *kept);
/* v1.9 collections (options of albums, artists, genres, folders, playlists). */
unsigned player_folder_count(const char *path);
int player_resolve(const struct QueueSource *src,uint32_t i,char *out);
unsigned player_play_next_free(void);
/* v1.6 Settings > Show songs as: 0 Titles (default), 1 File Names. */
unsigned player_song_files(void);
void player_song_files_toggle(void);
int player_menu_exchange(unsigned menu,unsigned id_a,unsigned id_b);
int player_delete_configuration(void);
int player_delete_library(void);
void player_playlist_removed(uint32_t id,uint32_t index);
void player_playlist_moved(uint32_t id,uint32_t from,uint32_t to);
/* UI entry points (ui.c). */
void ui_init(void);
void ui_boot(void);
struct InputFrame;
void ui_frame(const struct InputFrame *in);
