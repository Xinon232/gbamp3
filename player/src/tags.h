/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v1.9.1: change the artist, album or genre tag of a
 * song file (MP3: ID3v2 frame TPE1 / TALB / TCON; GAC: GACMETA3 field). */
#pragma once
#include <stdint.h>
enum {TAG_ARTIST,TAG_ALBUM,TAG_GENRE};
/* Longest accepted value in bytes (UTF-8): what the library and GACMETA3 keep. */
#define TAG_MAX_ARTIST 95
#define TAG_MAX_ALBUM 95
#define TAG_MAX_GENRE 63
/* Sets `field` of the song at `path` to the UTF-8 `text` (writes must be
 * enabled). `buf`/`cap`: work memory (at least 8 KB; the file handles live there too) for moving data.
 * MP3: the tag is rebuilt in place when it fits in its old size (padding
 * included); otherwise the file is written again as PATH.tmp with 2 KB of
 * padding, the original renamed PATH.old, the new file renamed PATH and the
 * old one deleted, so a complete file exists at every step. The file date is
 * changed so Update Library reads the tags again.
 * Returns 0; -1 on a read or write failure (in place: the tag may be
 * damaged); -2 when the tag cannot be changed safely (left as it is). */
int tag_set(const char *path, unsigned field, const char *text, uint8_t *buf, unsigned cap);
