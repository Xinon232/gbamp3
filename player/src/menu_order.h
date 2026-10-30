/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Stable menu identities and per-menu permutations. */
#pragma once
#include <stdint.h>
enum {MENU_HOME,MENU_SETTINGS,MENU_OPTIONS,MENU_PLAYLIST_OPTIONS,MENU_COUNT};
enum {OPTION_PLAY,OPTION_NEXT,OPTION_ADD,OPTION_INFO,OPTION_REMOVE,OPTION_RENAME,OPTION_DELETE,OPTION_CONVERT};   /* v1.9: file Rename / Delete; v2.0 Convert */
/* v1.9 Playlist Options: stable IDs 0-2 as before, 3-6 new (shown after Play). */
enum {PLOPT_PLAY,PLOPT_RENAME,PLOPT_DELETE,PLOPT_SHUFFLE,PLOPT_NEXT,PLOPT_ADD,PLOPT_INFO,PLOPT_CONVERT};   /* v2.0 Convert */
/* v2.0 Settings: Convert all (stable ID 7, after Update Library). */
#define SETTING_CONVERT_ALL 7
/* v1.9: Song Options shows Rename (bit 0) / Delete (bit 1) only when the
 * Secret Settings switches allow them (set by the user interface). */
extern unsigned menu_order_files;
/* v2.0: Convert rows shown (set by the user interface): bit 0 Settings
 * Convert all, bit 1 Song Options, bit 2 Playlist Options. */
extern unsigned menu_order_convert;
/* enabled: bit 0 Reorder menus on; bit 1 the v2.0 layout (eight entries in
 * Settings, Song Options and Playlist Options; set by reset and upgrade). */
#define MENU_ORDER_ON 1u
#define MENU_ORDER_V20 2u
struct MenuOrder {uint8_t enabled;uint8_t order[MENU_COUNT][8];};
void menu_order_reset(struct MenuOrder *m);
/* v1.6: orders saved before "Show songs as" existed get it before About. */
void menu_order_upgrade(struct MenuOrder *m);   /* v1.9: also the new Playlist Options entries; v2.0: the Convert rows */
int menu_order_validate(const struct MenuOrder *m);
unsigned menu_order_visible(const struct MenuOrder *m,unsigned menu,unsigned current,unsigned in_playlist,uint8_t ids[8]);
/* Swap visible neighbours, retaining the relative order of hidden items. */
int menu_order_swap(struct MenuOrder *m,unsigned menu,unsigned from,unsigned to);
int menu_order_exchange(struct MenuOrder *m,unsigned menu,unsigned id_a,unsigned id_b);
