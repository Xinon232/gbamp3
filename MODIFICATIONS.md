# Modification record

## 12.0 — Halim Jarrar, 2026-11-11

Based on 11.0. No third-party source in this repository changed (gbaSD
unchanged, 3ee9d37).

- Folders while music plays (`folder.c`, `folder.h`): one pass reads a folder
  when it is opened and fills every cursor bound to it (the list's folders
  and songs, the queue's songs; the queue takes a listing over from the
  list). The pass reads the folder's sectors itself on FAT16/32 and exFAT
  (`pass_raw`; FatFs's f_readdir took 1.6 ms of processor time per
  long-named entry on FAT32 and 2.7 ms on exFAT); an entry it does not take
  as it is (other than ASCII in a short name, a surrogate pair, a name over
  255 bytes, a torn exFAT entry set), a folder it cannot follow and FAT12 go
  through f_readdir. `folder_yield()` (the audio service) runs before every
  sector and every 24 levels of the sort (a bottom-up heapsort on 16-bit
  words). A sort record ends in the entry's place in the folder (the entry
  number, in pairs on exFAT) instead of its index, and each cursor keeps the
  folder's cluster numbers (256, then every 2nd, 4th ...): a row is one seek
  and one f_readdir. A folder too large for records keeps the places alone
  in card order (every 2nd, 4th ... when they do not fit); over 65536 places
  the checkpoints of 11.0 serve. The order is unchanged (the same folded key
  length, then the card order).
- `folder_keep` / `folder_open` (`storage_opener` in `storage.c`): the song
  the queue resolved last is opened by its directory entry (gbaSD's
  f_open_entry), for its tags (`load_meta`) and its audio, without a search
  of its folder (0.7 to 1.4 s per song change in a folder of 2000).
- Letter steps (L + Up / Down) in Folders take the letter from the sort key
  the listing keeps (`folder_key`), not from the card (about 20 rows read
  per step before).
- `folder_changed()` after Rename, Delete (files, folder songs), tag
  changes and conversions; `folder_count_path()` counts the songs of
  another folder (Folder Options) while a folder plays.
- `main.c`: `pump()` is not entered again from a yield inside the next-song
  lookup; the playing folder's listing is read between frames
  (`queue_dir_ready`), not inside the decoder.
- `text.c`: `fold_point()` folds one code point (the pass folds names it holds
  as UTF-16); `fold_next()` gives the same result as before.
- Album cover long scan (`library.c`, `library.h`; Secret Settings row 9 in
  `ui.c`; `main.c`; `state.c` / `state.h`): with `library_long_scan` 0 the
  index is byte for byte as before (album_covers() unchanged). With 1,
  `long_covers()` gives every song with a picture (MP3 JPEG, GAC cover) the
  cover of its own picture, in record id order. Identical pictures share one
  COVER_REC record: a picture key (length and FNV-1a of the first 1 KB and
  last 512 bytes; a GAC's whole cover) finds a record without a decode, a
  record key (FNV-1a, then the bytes compared) folds equal results; both in
  a 1024-slot direct-mapped table in the last 8 KB of the sort buffer.
  Unchanged files reuse the previous index's record (the one they provided,
  or any of theirs when that index was built with the long scan).
  `library_cover_rebuild` reuses none. State: `new_playlist_top` bit 3 the
  switch, bit 4 how the index's covers were built (a difference sets the
  rebuild; after a successful update bit 4 follows bit 3); values over 31
  (was 7) are reset. Delete configuration / Delete library index are rows 10
  and 11.
- Memory: IWRAM end 0x03006de4 (unchanged), EWRAM end 0x0203e208 (11.0:
  0x0203d0e8; the long scan's two switches: 8 bytes).
- Tests (long scan): `tests/test_v12_long_scan.py` (host: Off byte for byte
  as 46cb806, On per-song covers, shared records, rescan, rebuild) and
  `tests/test_v12_long_scan_guest.py` (Now Playing, On and Off again).
- Tests: `tests/test_v12_folder.c` / `.py` (host, in `run_host.py`) and
  `tests/test_v12_folders_guest.py` (in `run_acceptance.py`), both failing on
  11.0. Measured with 11.0 (3aa9b83) and 12.0, frames at 60 a second, all
  four cart models, FAT32 and exFAT, MP3 and GAC:

| folder | underruns 11.0 -> 12.0 | open (frames) 11.0 -> 12.0, MP3 / GAC | page wait | bottom/scroll wait | play+Next wait |
|---|---|---|---|---|---|
| 50 | 157 -> 0 | 42 / 41 -> 23 / 34 | 49 -> 22 | 28 -> 14 | 39 -> 14 |
| 500 | 147 -> 0 | 318 / 315 -> 496 / 214 | 85 -> 27 | 30 -> 14 | 242 -> 15 |
| 2000 | 331 -> 0 | 1149 / 1146 -> 1973 / 751 | 136 -> 31 | 31 -> 15 | 836 -> 14 |
| 5000 | 546 -> 0 | 2764 / 2759 -> 4366 / 1621 | 547 -> 32 | 44 -> 15 | 1972 -> 15 |
| 300 subfolders | 216 -> 0 | 183 / 184 -> 251 / 125 | 95 -> 26 | 38 -> 15 | 76 -> 75 |

- Version 12.0: About page 1, CONTROLS.md / gbamp3-controls.pdf, README.md,
  README.public.md, RELEASE_NOTES.md, BUILDING.md, the Windows converter
  README, `.github/workflows/release.yml` (VERSION v12.0).

## 11.0 — Halim Jarrar, 2026-11-04

Based on 10.0. No third-party source in this repository changed (the
FatFs changes of gbaSD 3ee9d37 come with the submodule, below).

- Keys (`input.c`, `input.h`, `main.c`): 10.0's "a held key released and
  pressed again between frames is a press" is undone; the key latching is
  exactly as before it again. The sound interrupt's key poll latches new
  presses only, `input_moved()` is gone, and a key pressed and released
  entirely between two UI frames still counts as one press (v0.8). IWRAM
  unchanged (every IWRAM symbol at the same address). `tests/test_v22_input.c`
  is replaced by `tests/test_v11_input.c`, which checks the old behaviour
  (it fails on the 10.0 sampler). `test_v22_boost_stress_guest.py` saves
  (taps during state saves, 50 busy polls): the taps are 8 frames apart
  instead of 1; with 1 frame a tap released and pressed again while a save
  blocks the loop is one held press again (24 of 24 on the SuperCard model,
  15 and 14 of 24 on the Omega and EverDrive models).
- Hold START to unlock (`state.c`, `state.h`, `main.c`): On for every card
  in 11.0, also where it was switched Off before. flags bit 7
  (`STATE_HOLD_RESET_DONE`, unused since the v2.2 test build; 10.0 clears it
  on loading) marks "reset to On once": a saved state without it loads with
  the switch On (new_playlist_top bit 1 cleared) and the bit set, so an Off
  chosen afterwards is saved with the bit and kept. Defaults (fresh card,
  Delete configuration) have the bit set. Same GBAST003 layout; a 10.0 ROM
  reading an 11.0 state ignores the bit (it would reset once more after
  going back to 11.0). ROM code only. `test_v22_hold_unlock_guest.py`: the
  saved Off has bit 7 set; v10off (a 10.0 state with Off) loads On (row and
  1 s hold, playing and asleep), switched Off again it stays Off after a
  restart; delete (Delete configuration on an Off card): On, then Off is
  kept. Fails on the 10.0 ROM. `test_v18_guest.py` expects flags 0x80.
- Previous song (`queue.c`): in Normal mode Left on the first song of the
  list goes to the last one, as Repeat All's wrap (before: nothing, e.g.
  after the end of the list, Right to the first song and Left back). The
  playing state is unchanged (stopped: the last song is shown; playing: it
  plays); `queue_prev()` skips unplayable entries across the wrap too.
  Repeat One and Shuffle unchanged. `test_core_host.py` (queue cases) and
  `tests/test_v11_prev_guest.py` (in `run_acceptance.py`) fail on 10.0.
- Song lists, choosing a song (`ui.c`): a song chosen in a song list makes
  that list the queue (Next / Previous are its neighbours as listed). The
  song that is already playing, chosen again in another list, only resumes
  and Now Playing opens; its queue (the list it was started from) stays, as
  in 10.0 (the owner's decision: an 11.0 test build made the chosen list
  the queue with `queue_adopt()` / `player_adopt()`; both are removed
  again). In a folder with subfolders, `play_from()` compared the song
  index as a row, i.e. the wrong song: choosing the song after the playing
  one resumed the playing one; it now takes the row. ROM code only, IWRAM
  unchanged. `tests/core_host.c` "choose" (chooses a row as `play_from()`
  does) and `tests/test_v11_list_order.py` (host, in `run_host.py`: 111
  cases over the title, file name, album, artist and genre orders, a
  folder and a playlist, names that sort differently by case, accents,
  numbers, "the " and punctuation; the playing song chosen again keeps its
  queue); `tests/test_v11_list_order_guest.py` (exact ROM, in
  `run_acceptance.py`: Songs, Search, an artist, an album, a genre, a
  playlist and a folder, each with a song chosen while another plays (its
  list becomes the queue) and with the playing song chosen again (its
  queue stays); and the row after the playing song in a folder with a
  subfolder). The guest test fails on 10.0 (the folder cases; the playlist
  case through Left on the first song). `tests/sd_runner.c`: TRACE_ADDR /
  TRACE_WORDS log guest memory per frame (the current song path).
- Large SD cards (`diskio.c`): `disk_initialize()` refused every card over
  0x4000000 blocks (32 GiB) on the SuperCard path ("No SD card" with a 64 GB
  SanDisk Ultra, exFAT, in a SuperChis Prime). The cap is gone: any card the
  driver reports with a nonzero block count works, up to FatFs's 32-bit
  block numbers (2 TiB); a standard-capacity (non-SDHC) card over 2 GiB is
  still refused. Audit of the other size arithmetic for large cards (no
  further change needed): `disk_read`/`disk_write` bounds use 32-bit block
  numbers without overflow (`n > block_cnt - s`), also with the Omega and
  EverDrive drivers' 0xffffffff; FatFs never asks for the sector count (no
  mkfs/fdisk); gbamp3 does no free-space arithmetic (no `f_getfree`);
  requests of whole 128 KiB (and 1 MiB) clusters are split into 16-sector
  commands; the fast-seek link maps, GAC seek checks, library record cache,
  playlist and state files use file offsets (files under 2 GiB), not card
  positions; the SuperCard, Omega and EverDrive drivers pass the 32-bit
  block number unchanged (SDHC/SDXC block addressing). IWRAM unchanged.
  `tests/test_disk_glue.c`: 32 GiB + 1 block, 64 GiB, 0x80000000 blocks and
  0xFFFC0000 blocks (the largest SDXC C_SIZE) mount and read their last
  block (fails on 10.0). `tests/test_v11_large_card_guest.py` (in
  `run_acceptance.py`, all three cart models): sparse 64 GiB cards (exFAT
  with 128 KiB and 1 MiB clusters, FAT32) and the 2 TiB - 128 MiB card, each
  with its partition beyond 32 GiB (from LBA 0x5000000, 0xF0000000 on the
  largest), mount, Update Library, play and seek a GAC and an MP3, fsck
  clean; on the SuperCard model the card size comes from the CSD
  (`tests/sd_runner.c`: SD_C_SIZE). Fails on 10.0 (SuperCard).
- Playlists, "+ Add Songs" (`ui.c`, `main.c`, `player_api.h`, `state.c`,
  `state.h`): every playlist screen ends with a "+ Add Songs" row (s->count
  is the shown songs + 1). A on it opens the Search screen (aux 1, header
  "Add Songs", empty text: every song); START+A adds the selected song to
  the playlist (`add_to_playlist`, "Added") and stays; B pressed with
  nothing typed, or START+B, pops back to the playlist (entered again: it
  shows the new length). With Reorder menus On, A+Up / A+Down on it put it
  at the very top / bottom (`reorder`), like + New Playlist; songs never
  pass it. Its place is its own: bit 2 of `new_playlist_top` (bit 0 + New
  Playlist on top, bit 1 Hold START to unlock Off, flags bit 7 the hold
  reset marker); `state_load` keeps values up to 7 (before: up to 3). An
  older version reading a state with bit 2 set resets the byte to 0 (+ New
  Playlist at the bottom, Hold START On), as v1.8-v2.1 did for bit 1.
  Chosen over sharing bit 0 so that moving one row does not move the other
  on another screen. `player_set_new_playlist_top` keeps the other bits
  (it cleared bit 2 and up).
- Separator lines (`ui.c`): row flag F_SEP draws the top pixel line of a
  row grey (C_GREY, as gbavocab's line between groups of settings, 1 pixel
  across the screen): the row below + New Playlist / + Add Songs when it
  is on top, the action row itself at the bottom; only with other rows.
  The line belongs to its row, so scrolling (which copies whole rows) moves
  it, and a selected row is blue below it (the line stays grey).
- Missing playlist songs (`ui.c`, `playlist.c`, `playlist.h`, `main.c`):
  `check_rows` hides an entry whose file `f_stat` cannot find (up to 128
  per open playlist, then grey as before): `pl_entry` maps a shown row to
  its entry, the list is redrawn. Play, Song Options (Remove, the queue
  index), A+Up/Down (`playlist_swap` of the two entries) and the queue use
  entry indices; the queue (whole playlist) skips missing files as before.
  `queue_playlist_moved` now exchanges the two entries (identical for
  neighbours; a hidden entry between them stays). `check_rows` also runs
  with nothing playing (it waited for buffered audio, which never comes),
  and a playlist shorter than the screen is checked at all (`rc_need` kept
  the rows past its end, which are never read, so `check_rows` never ran).
  Update Library (`player_update_library`, after a successful scan, phase
  9 "Checking playlists") calls `playlist_prune` for every playlist: a
  read-only pass over up to 8192 entries marks those whose `f_stat` says
  FR_NO_FILE / FR_NO_PATH (an entry whose path is one of the songs just
  scanned is kept without a card search: their FNV-1a path hashes, sorted,
  in the library's 64 KB sort buffer while folder sorting is paused; with
  f_stat for every entry, 12 playlists of 60 songs in a folder of 151
  added about 10 s to Update Library on the modeled card) (an entry failing its checksum or with an invalid
  name is kept; any other result is a read error and stops this playlist,
  nothing removed), then one change (`modify`, OP_PRUNE: both copies, the
  second first, header cleared first and written last, as every playlist
  change) drops them; repeated for the next window. Removals are reported
  in order (`queue_playlist_removed`), so a queue from that playlist keeps
  its song; its saved state follows. ROM and EWRAM only (1 KB mark map,
  0.5 KB hidden-entry list).
- Add to Playlist (`ui.c`): opened from Song Options of a song in a
  playlist (p2), the target list leaves that playlist out (its index + 1 in
  p0, found by id on entering).
- Playlist Options (`menu_order.c`, `ui.c`): Add to Playlist (PLOPT_ADD, ID
  5) is no longer shown (it added a playlist's songs to another playlist,
  which the owner found confusing as "a playlist inside a playlist"). The ID
  keeps its slot in saved orders, so `menu_order_swap` moves the shown
  neighbours around it and `menu_order_validate` / `upgrade` are unchanged;
  6 rows (7 with Convert).
- IWRAM unchanged by all of the above (`__iwram_end` 0x03006de4, every
  IWRAM symbol at the same address); EWRAM .bss +3.1 KB (`__bss_end`
  0x0203c8e0 (0x0203bc98 before), limit 0x0203f000).
- Tests: `tests/test_v11_prune.py` (host, in `run_host.py`; core_host
  `plprune`, `failread`, `failreadsfrom`, `stnpt`): what is removed and
  kept (absent file, absent folder, duplicate, an entry failing its
  checksum), both copies equal, the reported indices; a read error at each
  of the operation's reads (once, and every read from there on): the old or
  the pruned list, never an existing song removed, a later prune finishes;
  a power cut after every sector write; 8200 entries (two windows); the
  state bit. `test_v19_guest.py` (Playlist Options: 6 rows) and
  `measure_playlists.py` (the two missing songs of Mix 01 are gone after
  the Update Library at its start: no grey row) follow the change.
  `tests/test_v13_policy.c`: Playlist Options without Add to
  Playlist, a swap across the hidden ID. `tests/test_v11_playlists_guest.py`
  (exact ROM, in `run_acceptance.py`): hidden missing songs and playback
  through them, Update Library pruning (and one run per card read of the
  playlist check with that read failing: `SD_FAIL_READ`, `EZFO_FAIL_READ`,
  `EDGBA_FAIL_READ`; `sd_runner.c` logs the read count the model's knob
  uses as column `sdr`; on the SuperCard model the error reaches FatFs and
  Dead stays whole, the Omega and EverDrive drivers retry the read until it
  succeeds), + Add Songs (Search, Added, B,
  START+B, the playlist file), its position top / bottom with Reorder menus
  On and the saved byte, + New Playlist on top, the separator line in
  screenshots (both lists, both positions, scrolled, selected row under the
  line), Add to Playlist without the current playlist, Playlist Options with
  6 rows and a saved reorder across the hidden ID. Fails on 33cd961 (24
  checks); `test_v11_prune.py` does not build there.
- gbaSD 3ee9d37 (submodule `gbasd` 9cc52f8 -> 3ee9d37; its
  `docs/fatfs.md` changes 6-8, `docs/large-cards.md`): FatFs's exFAT
  free-cluster search and `f_getfree` look at the allocation bitmap a word
  at a time (`GBASD_FF_FAST_EXFAT_FREE`, default 1), 32 MiB exFAT clusters
  mount (`GBASD_FF_BIG_CLUSTERS`, default 1: `FATFS.csize` is a DWORD,
  `FATFS` 0x23c -> 0x240 bytes), and `ffconf.h` sets
  `GBASD_FF_MULTI_BUF 4` (a 2 KiB buffer in EWRAM .bss: the bitmap is read
  ahead 1, 2, 4 sectors per `disk_read`, a new folder cluster is cleared 4
  sectors per `disk_write`; `#ifndef` so that gbaSD's `bitmap_fuzz.py` can
  set its own sizes). `main.c`: `gbasd_alloc_hint(storage_fs(),
  LIBRARY_DIR)` right after `library_repair_dir(LIBRARY_DIR)` at the start
  (one read of GBAMP3.SYS; FatFs's search for free space then starts after
  the highest cluster of GBAMP3.SYS's files instead of at cluster 2: exFAT
  stores no such hint; nothing on FAT32 with a valid FSInfo, or when the
  folder does not exist). `build.py`: `gbasd/fatfs/alloc_hint.c` in
  `FATFS_SRC` and the `THUMB` set (ROM, Thumb -Os, `gbasd_alloc_hint` 626
  bytes). Before, on an exFAT card whose free space is at its end, the
  first allocation after a start read the bitmap from the start bit by bit
  (a nearly full 2 TiB card: 4096 sectors; Update Library 6952 frames on
  the SuperCard model, 6936 Omega, 6927 EverDrive); now 31 / 34 / 38
  frames. 10.0/0321ed2 refused 32 MiB clusters (no card). The first start
  on a 32 MiB-cluster card without GBAMP3.SYS clears the new folder's
  65536 sectors: 7126 / 3924 / 11244 frames on the models (no change
  wanted there: it happens once, before any audio).
- Memory: IWRAM unchanged (`__iwram_end` 0x03006de4, limit 0x03006e00; all
  177 IWRAM symbols at the same address, checked with `nm`; no FatFs code in
  IWRAM). EWRAM .bss +2056 bytes (`__bss_end` 0x0203c8e0 -> 0x0203d0e8,
  limit 0x0203f000: the 2 KiB buffer `gbasd_mbuf` and `fs` +4). ROM code
  +192 bytes net. `DRIVER_PROVENANCE.json`: `local_commit` 3ee9d37 (the
  driver files are unchanged).
- Tests: `tests/test_v11_large_card_guest.py` also runs gbaSD's large-card
  kit cards (`gbasd/tests/large_cards/card.py set --end --keep-fill
  --tree`: exfat-64g, exfat-2t, exfat-2t-sda, exfat-64g-1m, exfat-2t-32m,
  fat32-32g, with /gbamp3 and GBAMP3.SYS at the far end and the free space
  only after them, plus exfat-2t-32m without GBAMP3.SYS), a third start per
  card that adds a song to a playlist (Songs > START > Add to Playlist),
  and limits: Update Library within 100 frames of the A press, the main
  loop never held over 35 frames from Add to Playlist on, the first start
  without GBAMP3.SYS within 18000 frames (`LC_MEASURE=1`: report only).
  On 0321ed2 it fails (exfat-64g, exfat-2t, exfat-2t-sda over the limit,
  exfat-2t-32m not mounted). `run_host.py` runs gbaSD's
  `tests/fatfs/bitmap_fuzz.py` with gbamp3's `ffconf.h` (3000 cases: the
  fast search against R0.15's).
- Licence: NOTICES.md: Halim Jarrar grants the same additional permission
  for his files in `gbasd/`. No own code is described as MIT any more (the
  10.0 entry above no longer names the removed licence file).
- Version 11.0: About page 1, CONTROLS.md / gbamp3-controls.pdf (also the
  new playlist rows, Left on the first song, the queue, cards up to 2 TB;
  "See BUILDING.md" now points to the README's Build the ROM section, as
  BUILDING.md is not published), README.md, README.public.md,
  RELEASE_NOTES.md ("# gbamp3 11.0", also listing 10.0's changes, which
  were not released), `.github/workflows/release.yml` (VERSION v11.0),
  `packaging/windows/README-WINDOWS.txt`. The converter's own label has no
  app version (unchanged).

## 10.0 — Halim Jarrar, 2026-10-29

Based on v2.1.1 and the gbaSD / exFAT / Hold START to unlock changes below.
No third-party source changed.

- Licence: gbamp3's own code (player, GBAWriter input engine, tools,
  converter, build scripts, tests) is GPL-3.0-or-later with the additional
  permission under GNU GPL version 3 section 7 for linking with the Helix MP3
  decoder (NOTICES.md); every file header says so. The separate licence file
  of the earlier releases is removed. Helix, SuperFW, shine, FatFs and gbaSD files and their notices
  are unchanged.
- `ui.c`: About shows "gbamp3 10.0", the copyright, the licence, "No
  warranty.", the source link (github.com/Xinon232/gbamp3) and the credits
  (same 8 pages and keys; page 7 is "More credits").
- `CONTROLS.md`, `gbamp3-controls.pdf`: version 10.0 and an "About / licence"
  section at the end (same seven pages).

## v2.1.1 — Halim Jarrar, 2026-10-14

Based on v2.1. No third-party source changed.

- `ui.c`: Now Playing Left / Right hold seeks 10 s (was 5 s); About shows
  v2.1.1.
- `CONTROLS.md`, `gbamp3-controls.pdf`: seek step 10 seconds.
- `tests/test_v211_seek_guest.py`: exact-ROM seek steps and timing.

## v2.1 — Halim Jarrar, 2026-10-10

Based on v2.0.1. No third-party source changed.

- `gfx.c` / `gfx.h`: mode 4 only (mode 3 and the mode switch removed);
  interface palette entries 1-15; `gfx_cover` draws a reduced cover with
  entries 16-255, its colours applied with the page switch (`pal_apply`).
- `ui.c`: Now Playing on the hidden page; a new song is shown once all its
  sections are drawn; the marquee always redraws (`text_scroll` removed from
  `text.c`); About shows v2.1.
- `library.c`: index v7 (only v7 read); album covers from MP3 pictures or
  GAC covers, stored as `COVER_REC` records (`cover.h`).
- `jpeg_dc.c`: `jpeg_cover_reduce`, the median cut to 240 colours, as ARM
  in the cover overlay with its pixels in the decoder's IWRAM scratch.
- `main.c`, `storage.c`: covers of all songs come from the library (GAC art
  is no longer copied at playback).
- Tests: `tests/cover_quant.py` (the reduction in Python), cover pixel
  checks against it (`test_v098_guest.py`, also a GAC cover),
  `tests/measure_np_track.py` (a new song appears whole), Now Playing switch
  gate 0 white or partial frames; host tests for library v7.

## v2.0.1 — Halim Jarrar, 2026-10-09

Based on v2.0. No third-party source changed.

- `gfx.c`: page-to-page copies in bands of 8 lines (`copy_lines`).
- `main.c`: the idle Halt runs with interrupts enabled, and not while a
  track's end drains.
- `ui.c`: About shows v2.0.1.
- Tests: `tests/sd_runner.c` counts samples taken from an empty sound FIFO
  (`fifo-starved.txt`); `test_v15_scroll_stress_guest.py` requires zero and
  has a mono MP3 case (`mp3mono`, made with ffmpeg); `run_acceptance.py`
  runs it.

## v2.0 — Halim Jarrar, 2026-10-07

Based on v1.9.1.

- **Helix (RPSL) source change, marked "2026-10-02" in the files:** output
  mode 3 (`MP3SetOutputMode(h, 3)`, `rt_config.h`): every band of every
  encoded channel at the source rate, for converting songs; the mono mixing
  paths are skipped in that mode (`mp3dec.c`, `dequant.c`, `subband.c`).
  Playback modes 0-2 are unchanged. Build placement only: `dct32_core_arm.S`
  now runs from the MP3 overlay in IWRAM.
- **shine (LGPL v2) added** in `player/vendor/shine` (MP3 encoder for GAC ->
  MP3 conversion); its changes are listed in
  `player/vendor/shine/MODIFICATIONS.md`, generated tables come from
  `tools/shine_tables.c`.
- Independent player changes: conversion on the GBA (`convert.c`,
  `convert_gac.c`, `gac_enc.c`, `gac_enc_arm.S`, `resample.c`, `rs_dot2.S`,
  `mp3enc_arm.S`, `jpeg_enc.c`, `gac4.c` 16-bit output, `overlay.c`
  conversion overlays, `player.ld`), conversion menus and progress (`ui.c`,
  `menu_order.c/.h` v2.0 layout, `main.c`, `state.c`), Letter Box Off / On /
  Only L+DPAD, About pages without wrap, B on "Sure?" taking only the
  question back. New tests: `test_gac_enc.py`, `test_convert.py`,
  `test_convert_gac.py`, `test_full_decode.py`, `test_v20_ui_guest.py`,
  `test_v20_conv_guest.py`.

## v1.5 — Halim Jarrar, 2026-09-30

Based on v1.3 (v1.4 not included). No Helix (RPSL) source change.
Independent player changes: `input.c/.h` long-list step schedule (row
speed-up, page and letter repeats, steps accumulated over slow frames);
`ui.c` long-list navigation (pages, speed-up, wrap on a fresh press, letter
mode with binary search over the sorted lists, letter box drawn around by
row painting and multi-row DMA scroll), Now Playing track changes drawn in
place, Secret Settings Auto-update row and `:` labels, navigation telemetry;
`folder.c/.h` `folder_sorted()`; `state.c/.h`, `main.c`, `player_api.h`
Auto-update flag in a former padding byte of GBAST003. New tests:
`test_v15_input.c`, `test_v15_nav_guest.py`, `test_v15_autoupdate_guest.py`,
`test_v15_scroll_stress_guest.py`, `measure_flicker.py`.

## v1.3 — Halim Jarrar, 2026-09-28

No Helix (RPSL) source change. Independent player changes: `ui.c` repaints only changed Search rows, adds About page 5 and Secret Settings, stable-ID menu actions and cancellation/confirmation, and refreshes Song Options on playback changes; `menu_order.c/.h`, `state.c/.h` and `main.c` save the four menu orders and switch in GBAST003 (older state still loads); `state.c`, `library.c`, `queue.c` and `main.c` support separately confirmed configuration/index deletion without touching music or playlists or discarding Play Next on index deletion. New host and exact-ROM tests, updated controls/PDF and release documentation accompany these changes.

## v1.2 — Halim Jarrar, 2026-09-27

Independent (no Helix change):
- `library.c/.h`: index v5 (GBALIB5) with a Song Title word index (record
  id, word offset, duplicate byte and 8 key bytes per word start, sorted with
  the existing on-card merge sort, now generalized to several records per
  song); v4 libraries still read; word search API.
- `ui.c`: Search in word mode for Song Title, word-mode rows and queues;
  Shift/Caps drawn at a fixed place; "Indexing titles" progress; About.
- `queue.h`, `main.c`: QUEUE_WORDS (saved state layout unchanged).
- `diskio.c`: requests over 16 sectors are split into 16-sector commands
  instead of rejected (whole 16/32 KB clusters).
- Tests: `test_word_search_host.py`, `test_word_search_guest.py`,
  `measure_word_search.py`, `core_host.c` word commands, `test_disk_glue.c`.

## v1.1 — Halim Jarrar, 2026-09-27

**Helix (RPSL) source change, marked 2026-09-27 in the file:**
- `rt_config.h` (`RT_SHIFT_FOR`): stereo synthesis at 1/8 rate for 32 kHz
  (was 1/4) and 1/4 for 16-24 kHz (was 1/2), the lowest rate that carries the
  retained bands. Retained bands and 44.1/48 kHz output unchanged.

Independent: MP3 output at 32768 Hz in both modes (`player.h`); 32.8 kHz
PWM for all MP3; 12-sample FIFO slack at MP3 start like GAC; resampler with a
shift weight and Volume Boost per decoded sample (`main.c`); MP3 start fill,
refill and UI-wait thresholds in samples doubled (same time); hidden system
folder created at startup (`main.c`); About version (`ui.c`).

## v1.0 — Halim Jarrar, 2026-09-27

No Helix (RPSL) change. Independent: `ui.c` (progress bar drawn without
clearing, cached status line, Search field redrawn only when changed, A on
the loaded song opens Now Playing and resumes, START release after a START
command consumed), `main.c` (GAC cover loaded with the remembered song,
`player_active`), `player_api.h`.

## v0.9.9 — Halim Jarrar, 2026-09-26

No Helix (RPSL) change. Independent: MP3 output rates doubled
(`player.h`: 16384 Hz Stereo, 65536/3 Hz Mono; timer periods 1024/768),
65.5 kHz PWM for Mono MP3, resampler scale limit raised to 262144, MP3 start
prefill 2048 samples (`main.c`), UI rate labels (`ui.c`).

## v0.9.8 — Halim Jarrar, 2026-09-26

**Helix (RPSL) source changes, each marked 2026-09-26 in the file:**
- `huffman.c` (DecodeHuffman): clears decoded lines only up to what later
  stages read (the larger retained limit of the two channels, the whole
  spectrum when intensity stereo is on) instead of always to 576. Output
  bit-identical (all 16 fixtures on the host, 60 s stress DAC streams).
- `rt_config.h`: `RT_MONO_BANDS` — output mode 2 keeps 8 bands (7 at 48 kHz)
  for mono output at >= 32 kHz; modes 0/1 unchanged.
- `mp3dec.c` (MP3SetOutputMode): accepts mode 2.

**Helix build-level changes (no source change):** `scalfact`, `bitstream`,
`stproc`, `mp3dec`, `dequant` and `mono_hybrid` are built as Thumb (ROM);
`xmp3_UnpackScaleFactors` no longer goes to the IWRAM overlay (LAYOUT.json);
`mp3dec.c` is built with `-Dmemcpy=mp3_memcpy -Dmemmove=mp3_memmove`;
test builds only: `PROFILE_MP3=1` defines Helix's `PROFILE`, whose hooks
include `player/src/systime.h`.

**Independent:** `player/src/jpeg_dc.c` (+ `tools/jpeg_dc.py`), the
third IWRAM overlay `.ov_jpeg`, `mp3_copy.c`, library index v4 with album
covers (`library.c`), APIC/PIC and bitrate parsing (`meta.c`), MP3 covers at
track start, Song Info bitrate, per-song Mono band choice (`main.c`).

## v0.9.5 — Halim Jarrar, 2026-09-26

No Helix (RPSL) source or placement change. Independent: Volume Boost
(`tools/boost.py`, generated `player/src/boost_table.h`, `player/src/boost.h`;
gain + limiter in the MP3 output path of `main.c` and in the GAC v4 output
stage of `gac4.c`, plus the ARM loop `gac4_boost64` in `gac4_arm.S`), the
Settings row and Now Playing keys (`ui.c`), state file GBAST002 and the Play
Next file QUEUE0/1.DAT (`state.c`, `queue.c`), and the play/pause icon.

## v0.9.0 — Halim Jarrar, 2026-09-26

**Helix placement (RPSL code, build-level only; no Helix source line changed):**
the Helix objects' IWRAM code and constant tables (the former `.iwram*`
sections, the whole `.text` of huffman/imdct/polyphase(_arm)/
polyphase_mono_arm/alias_arm, and the three LAYOUT.json placements) are
renamed by `player/build.py` / `player/tools/placement.py` into `.ov_mp3.*`
and linked into an IWRAM overlay region (`player/player.ld`) that the MP3
and GAC decoders share. `player/src/overlay.c` copies the MP3 overlay from ROM
before `MP3InitDecoder()`; MP3 output is bit-identical to v0.8.3 in the
emulator stress runs (V090_VERIFICATION.md).

**GAC v4 (independent):** `tools/gac4.py`, `player/src/gac4.c`,
`gac4_arm.S`, generated `gac4_tables.h`; GAC v3 code removed from the player
and converter (`tools/gac3.py` kept under `tests/legacy/` for history). The
65536 Hz rate is removed from player, converter, tests and docs.

## v0.8.1 — Halim Jarrar, 2026-09-26

Converter only; the ROM is byte-identical to v0.8.0. `tools/gbacache.py`:
ffmpeg lookup (path, folder, PATH, or next to the program), FAT-safe publish
(`.part` file renamed when complete, replacing the hard-link publish), written
files re-verified, recursive folder input, output next to each MP3 or into a
folder keeping structure, `--replace` (delete MP3 after verified GAC),
`--overwrite`, existing outputs skipped. `tools/gbacache_gui.py`: Add folder,
subfolders, output choice, Replace (with confirmation), Overwrite, per-file
progress. The Windows package bundles an unmodified ffmpeg.exe from
imageio-ffmpeg with its license statement; its smoke test covers ffmpeg
auto-detection and a folder conversion with Replace. Release workflow builds
and publishes releases (`.github/workflows/release.yml`).

## v0.8.0 — Halim Jarrar, 2026-09-26

Based on v0.7.0 `6a15024c3bdb23c0135bb1a7af6de7d440148bcd`. The Helix MP3
decoder, the GAC v3 decoder and the audio engine (IRQ, rings, rates, WAITCNT
0x40c0) are unchanged.

Third-party code added or changed:
- FatFS R0.15 (ChaN): `ffconf.h` enables long file names (FF_USE_LFN 1, UTF-8
  API FF_LFN_UNICODE 2), fast seek (FF_USE_FASTSEEK 1) and f_chmod. `ffunicode.c`
  added unchanged (copied from GBAVocab). `ff.h`/`ff.c`: `FILINFO.fclust`
  (start cluster, set in get_fileinfo) and two helpers, `f_open_entry()`
  (read-only open of the entry f_readdir just returned) and `f_opendir_entry()`,
  so the library scan needs no second path lookup. Marked "gbamp3" in source.
- SuperFW font renderer (David Guillen Fandos, GPL-3.0-or-later):
  `vendor/superfw_font/` holds `utf_util.c/h`, `font_render.c/h`,
  `font_embed.h`, `hangul.h`, `compiler.h` unchanged (via GBAVocab). Only the
  glyph lookup and UTF-8 helpers are used; its framebuffer routines are
  unreferenced. `fonts-latin-greek-cyrillic.pack` is a subset of SuperFW's font
  pack (U+0080–U+024F, U+0370–U+04FF) made by `tools/font_subset.py`.
- GBAWriter v1.2.0 input engine (Halim Jarrar, GPL-3.0-or-later):
  `vendor/gbawriter/writer_core.cpp` unchanged; `writer_core.h` sets
  `TEXT_CAPACITY` to 63 for one-line fields.
- GBAVocab accent folding (Halim Jarrar, GPL-3.0-or-later): `src/fold_table.h`
  is generated from GBAVocab's `dictionary_accent_table.h` (488 mappings).

Independent code: new user interface (`ui.c`), text rendering on top of
the SuperFW lookup (`text.c`, IWRAM text blitter `blit.c`, 5x7 small font from
the old UI), input (`input.c`), ID3/GAC metadata reader (`meta.c`), library
index v3 (`library.c`: variable-length records, six permutations, group
tables, on-card merge sort), folder listing with alphabetical order
(`folder.c`), queue and playback modes (`queue.c`), playlists (`playlist.c`),
settings file (`state.c`), GBAWriter C bridge (`writer_glue.cpp`), path-based
storage (`storage.c`), player API in `main.c` (timer-based UI frame pacing,
key latching, audio-first scheduling), runtime helpers (`strcmp`, `strcpy`,
`__aeabi_lmul`, word-wise `memcmp`), converter GACMETA3 and default embedded
cover (`tools/gbacache.py`), tests. Removed: `browser.h`, `library_ui.c/.h`,
the v0.7.0 `ui.c` and HUD. Tests written for the removed code moved to
`tests/legacy/`. See V080_VERIFICATION.md.

## v0.7.0 — Halim Jarrar, 2026-09-25

Based on v0.6.0-library-prerelease `30dce773cf44d759d081867658ef703bfefb1ba6`.

Helix (RPSL), each change marked 2026-09-25 in the source:
- `dequant.c`: `RetainedLineLimit()` factors out the existing retained-band line
  limit so Huffman decoding and dequantization share it; `SkipSideChannel()`
  and a Dequantize path that keeps only the mid spectrum of mid/side granules
  in mono output and duplicates it for the existing shared mono transform.
- `huffman.c`: `DecodeHuffman()` decodes pairs and quads only up to the retained
  limit; the existing stuffing-bit advance keeps the stream position exact.
- `mp3dec.c`: skips the side channel's Huffman data (scale factors still
  unpacked) when `SkipSideChannel()` holds.
- `mp3common.h`: `skipSide` state and the two prototypes.
Stereo output is bit-identical; mono output is identical except guard bits now
measured on the transformed mid spectrum (1 LSB on 0.25% of one test clip).

Independent code: GAC v3 parser/decoder (`gac.c`, `gac.h`), converter
codec (`tools/gac3.py`), converter/GUI mono option, direct-to-ring noise-shaped
cache output in `main.c`, UI renderer moved to ROM (`ui.c`), v3 tests. See
V070_VERIFICATION.md.

## Local library candidate — Halim Jarrar, 2026

Based on the approved v0.5.0 cache experimental prerelease. GAC v2 adds bounded tags, prepared sort keys and optional PC-preconverted/checksummed 32x32 RGB555 artwork. No MP3 decoder, synthesis, cache ADPCM or output-rate algorithm was changed. Two fixed artwork buffers add 4096 bytes of EWRAM; IWRAM/stack gates remain unchanged.

New library.c/library_ui.c implement on-device two-slot persistent indexing, folder-local heuristic refresh, forced recursive scanning, eight-row category pages, browser-only Select hold, progress and cancel. Full scans reread metadata. Songs are opened FA_READ only. The normal folder browser remains available without an index.

Restored SuperFW scan-only sector-write assembly and CRC from the bundled donor diff, placed in EWRAM, with checked command/data response, stop, idle and status. diskio gates writes to index transactions and keeps reads available after a rejected write. FatFS is writable for index files only. The local f_abort_index_write extension discards dirty transaction buffers and invalidates the shared FAT window without retrying a failed write; only one writer and read-only other handles are permitted. No rename/unlink recovery is used. Partial FAT writes can still leak allocation or require repair after power loss; this is not a journal.

The register SD model captures real production writes, independently checks all four CRC16 lanes, flushes sectors to a persistent image, and supports read-only/rejected-write tests. Exact-ROM tests uncovered and fixed dirty-window and disk-state poisoning after write refusal. See LIBRARY_VERIFICATION.md for coverage and inherited audio failures.


## v0.5.0 experimental MP3 + GAC prerelease — Halim Jarrar, 2026

Adds optional PC-converted IMA stereo cache support alongside original MP3,
independent remembered cache sound modes, direct block seeking, bounded mixed-
format/rate transitions, and a GUI converter with an opt-in experimental
44150.6/65536 Hz checkboxes (mutually exclusive, default32768). The final user instruction removes lo-fi COMPLETELY from both MP3 and GAC.
Only clean Mono/Stereo choices remain; conversion itself remains clean.
This supersedes the briefly authorized experimental lo-fi integration. Historical
lo-fi work below is retained as provenance, not a current feature. Safe clock and MP3 support remain. The local library candidate below enables scan-gated index writes, never song writes.
Source, complete controls and all notices accompany the prerelease. Hardware
and subjective listening approval are pending; retained test failures are listed
in RELEASE_VERIFICATION.md rather than represented as passes.

## Local fixed lo-fi prototype — Halim Jarrar, 2026

Based on the protected reduced-synthesis source archive SHA256
`4d3a1b8f8592c88123a4e9a0077d5a5f9f5ffd2add1a4788d131c7604ecbad13`.
Adds independent fixed post-resampling EQ, separate channel histories and four
session-only sound modes, default LOFI MONO. Uses a shared IWRAM output helper
and size-optimized resampling to retain the existing memory/frame gates.
Silent browser startup, sample rates, reduced synthesis and storage remain.
This is a costly prototype, NOT accepted for performance or recommended for main.
No cheapest-implementation claim is made. User sound review is pending.
The closing audit changed documentation/analysis/packaging only, not production
code; a byte-identical ROM was rebuilt and current host/startup/control gates rerun.
See LOFI.md for exact limits, the corrected historical stereo comparison and
reproduction. All original notices remain; no new third-party artwork/music was
introduced and no publication occurred.

## Local browser/playback repair — Halim Jarrar, 2026

Based on public v0.4.0 `8d54dc6ac37a62584eff7cd4801a1a6a78dfab67`.
Fixed physical button wiring, explicit start versus pause-preserving seek/mode changes, automatic play order, independent active folder/seek identity, cooperative directory scheduling and empty-folder startup. Reworked the independently authored HUD and accessible Info/credits; no new third-party artwork was copied. One phase bit in the existing per-sample track tag distinguishes repeated-file boundaries without altering PCM. Decoder arithmetic/assembly, quality settings, SD driver, FatFS and safe WAITCNT remain unchanged.

Added real production adapter, FatFS and framebuffer regressions plus exact-ROM tests through the existing modeled-SD runner, current-controls whole-DAC checks and a documented still-failing stress gate. See CONTROLS.md and REPAIR_VERIFICATION.md. This is a local source commit only, not a new published release.

The following sections describe historical releases, not current controls.

## Stereo / higher-quality Mono — Halim Jarrar, 2026-09-12

Baseline: private v0.2.0-superfw-sdhc-prerelease, commit
`2b1b314faac0981eb422e1e2f4a62034ecc695ed`, ROM SHA256
`8973120e72b25958820eb1a366672cddceb4277e330e42986c508d18851e60a1`.

Added fresh-decoder output-mode API without changing encoded nChans, reservoir or
header semantics. Mono uses 6/12/24 retained bands versus Stereo 4/8/16; output
clock is exactly 32768/3 Hz. Identical current transform/window and previous
window metadata permit averaging spectra and overlap history before one IMDCT.
Otherwise both independent transforms run and their hybrid samples are averaged.
Histories remain valid across shared/fallback transitions; native mono is not halved.
All assembly sources and the entire SuperFW/FatFS/storage/header path are unchanged.
The full DCT core, newly needed for low-rate selected mono, stays in ROM; compact
history is not used. Bounded C layout/mono interpolation/IRQ common-case changes
fit IWRAM and preserve the complete inherited stereo PCM/output oracle.

Up changes mode with IRQ-quiesced position capture, decoder/ring reset, same-track
whole-second seek/prefill and preserved pause/lock semantics. HUD/help/credits rate
text and tests are updated. Diagnostic restart anchors allow independent whole-DAC
control verification without using guest PCM as expected output.

Tests add averaging/immutable API/window-history oracles, mode controls, complete
mono DAC references and independent FFmpeg quality analysis. The timing observer
models pinned mGBA PWM-slot scheduling from timestamps/observation ordinals, never
from expected sample data; all boundary observations remain checked. Rejected
16kHz attempts are retained outside release source. See QUALITY/VERIFICATION.

The following section describes the historical storage-only release, not a claim
that the new mode leaves every player/decoder source file unchanged.


## SuperFW/SDHC integration — Halim Jarrar, 2026-09-12

Baseline: public SD prerelease v0.1.0-sd-prerelease, original ROM SHA256
`5ed0be7ad379e41683d462c16e64b8378d9d4ff9dfda06bf83b3e16ad4d23301`.
The complete selected Helix decoder, mirrored history, adaptive quality,
resampler, FIFO/IRQ engine, MP3 framing, bounded storage/seek indexes and input
controls remain byte-identical. There is no compact-history or unrelated
algorithm change. Inherited Helix modification/Exhibit A notices remain RPSL.

Replaced the old libgba SDSC driver with a read-only subset of gbareader v1.3.0
(commit `723232ab364ea5771f7a905ab8f6c8d77a3a5f78`), derived from David Guillen
Fandos's GPL-3.0-or-later SuperFW. See `DRIVER_PROVENANCE.json` and
`driver-upstream.diff` for exact originals and scoped changes. No reader/book,
text-state, cache, save, SRAM banking, SD-write or persistence implementation
was imported. Unused declarations in upstream headers are not linked APIs.

Necessary corrections, tested with production driver C and/or actual ROM:
- Accept CMD3 RCA in Standby, not a non-Standby state.
- Reset state and fail ACMD41 readiness exhaustion; reject unknown CSD and
  inconsistent OCR/CSD capacity type. Correct SDSC C_SIZE/C_SIZE_MULT indexes.
- SDHC uses block addresses; SDSC uses byte addresses. Adapter bounds all reads
  to the reported card size and caps support at 32GiB (2GiB for SDSC).
- Preserve CMD18 + CMD12 multi-block reads, including stop after data timeout.
  Use standard CMD17 for one-sector requests with full six-byte response and
  eight trailing clocks. This avoids per-sector stop overhead on FAT16 files
  with one-sector clusters. Adapter batches at most 16 sectors per request.
- No interrupt masking or DMA/timer borrowing. Keep mode3 SDRAM plus SD window;
  ROM is below the register window. Use 0x40c0 WAITCNT during I/O, then restore.
  Disable fast mirror; retain original non-Lite read assembly. Place read and
  command loops plus CRC7 code in IWRAM with no compiler clones.
- Bound idle polling to 32,768 iterations and data-ready polling to 16,384;
  retain upstream response/init poll budgets. Slow cards may time out.
- Remove all C/assembly write routines and unused CRC16 implementations.
  Read CRC16 and response CRC7 remain unchecked, explicitly not certified.
- Update Credits source destination and SuperFW/FatFS attribution only.

The initial per-sector CMD18/ROM-command build underruns on the hardest track.
Command-only IWRAM and then complete command-path IWRAM avoid finite underruns
but still drain its queue and are rejected. The final standard CMD17 single-
sector / CMD18 multi-sector adaptation passes the original unchanged sustained
queue gate without changing quality. Raw rejected-run evidence stays outside
release staging, separate from final-ROM results.

## EZ-Flash Omega support (test build)

- `player/vendor/ezfo/ezfo_driver.{c,h}`: the EZ-Flash Omega SD driver shared
  with gbareader. Its per-command code is in a new IWRAM cart overlay
  (`player.ld`) that it shares with the SuperCard read code (crc.o,
  supercard_driver.o and supercard_io.o `.iwram*`, renamed `.cart_sc*` by
  `build.py`); `diskio.c` checks the cart once and `overlay.c` (`cart_use`)
  copies the right code in. The boot probe is Thumb code in EWRAM, the outer
  loops and detection are in ROM.
- `main.c`: `conv_key_tick` in EWRAM; the sound interrupt postpones the
  vertical-blank page switch (`gfx_flip_irq`, ROM) while the Omega kernel is
  mapped (`ezfo_busy`); `player_is_playing()` counts the moment between two
  songs (transition_done) as playing, so the lock screen does not put the
  GBA to sleep there (a race the Omega timing showed; it could also happen on
  a SuperCard).
- `ui.c`: the SD card page of About credits both drivers.
- Tests: `tests/ezfo_model.h` (Omega model), `EZFO=1 tests/build_runner.py`,
  `tests/check_ram_code.py`; `scsd_model.h` accepts every SuperCard mode
  (detection writes mode 1). Memory limits are unchanged: IWRAM end
  0x03006de4 (limit 0x03006e00), EWRAM end 0x0203efec (limit 0x0203f000).

## EverDrive GBA support (test build)

- `player/vendor/edgba/edgba_driver.{c,h}`: the EverDrive GBA SD driver shared
  with gbareader (a port of krikzz's edio-gba SD code, MIT). It runs from
  ROM as Thumb (the game ROM stays mapped while it works); `edgba_state` is
  the only RAM it adds (12 bytes of EWRAM).
- `diskio.c`: L+R at boot forces the SuperCard (only its code is copied into
  the cart overlay); otherwise the EverDrive is checked first, then the Omega,
  then the SuperCard. Reads and writes go to the driver of the cart found.
- `ui.c`: the SD card page of About credits the Omega and EverDrive drivers.
- Tests: `tests/edgba_model.h` (EverDrive model), `EDGBA=1
  tests/build_runner.py`; `test_disk_glue.c` stubs the EverDrive driver (with
  L+R held only the SuperCard code is loaded). Memory limits are unchanged:
  IWRAM end 0x03006de4 (limit 0x03006e00), EWRAM end 0x0203eff8 (limit
  0x0203f000).

## gbaSD and playlist copies (v2.2)

- The SD drivers (SuperCard, EZ-Flash Omega, EverDrive GBA) come from gbaSD,
  a git submodule (`gbasd/`, GPL-3.0-or-later), instead of
  `player/vendor/superfw`, `vendor/ezfo` and `vendor/edgba`. `build.py`
  compiles it with gbamp3's settings (short SuperCard waits, no write retry,
  at most 16 sectors per write, IWRAM/EWRAM placement) in the old link order,
  so IWRAM ends at 0x03006de4 and EWRAM at 0x0203eff8 as before. gbaSD's
  SuperCard driver accepts the ident and the standby answer to CMD3. The
  driver's host tests moved to gbaSD (`tests/test_superfw.py` runs them).
- Playlists keep two copies (`Pxxxxxxxx.DAT`, `Pxxxxxxxx.DA2`). A change
  clears the second copy's header, changes its entries and writes its header
  last with the next generation, then does the same to the main copy;
  readers use the main copy unless it is invalid (one file open, as before). The generation sits at byte 64
  of the header sector, after the v1 header, so old playlists are read as
  generation 1 and older versions still read new files. Before, removing or
  moving a song shifted entries in place, so a power cut could duplicate one
  song and lose another, and a damaged header lost the whole playlist.
  `tests/test_playlist_powercut.py` cuts the power after every sector write
  of add, remove, swap and rename (`core_host` `cutafter N`).
- `tests/legacy/` removed (history only; in the git history).
- The verification reports moved from the top level to `docs/verification/`.

## exFAT cards (v2.2)

- `ffconf.h`: FF_FS_EXFAT 1. Before, an exFAT card did not mount (most cards
  of 64 GB and more are sold exFAT).
- `ff.h`/`ff.c`: `FILINFO.fstat` (exFAT: 2 when the object is contiguous,
  "NoFatChain"), and on exFAT `get_fileinfo` also fills `fclust` and the size
  of a folder. `f_open_entry()`/`f_opendir_entry()` set the object's
  contiguous flag, and a folder's size, which exFAT needs to walk a contiguous
  object (it has no FAT chain).
- `ff.h`: `FSIZE_t` stays 32-bit with exFAT. 64-bit sizes made every seek do
  64-bit divisions in software, and a playlist opened a frame later, on FAT
  cards too. An exFAT file of 4 GiB or more reads as 4 GiB - 1 bytes long.
- `ff.c` `dir_find()` on exFAT reads the name hash from the stream entry in
  place and steps over entry blocks with another hash without copying them or
  checking their checksum (`dir_skip()`); a block with the hash is loaded and
  checked as before. Looking up a file in a folder of songs was several times
  slower than on FAT: checking a playlist's songs starved the audio (an
  underrun on the SuperCard and Omega models).
- The unused `f_abort_index_write()` is removed.
- `folder.h`/`folder.c`: the 32 checkpoints of a folder listing keep only the
  read position (16 bytes) instead of a whole `DIR` (48 bytes, 80 with exFAT).
  This pays for exFAT's larger `DIR` and its 608-byte entry buffer: EWRAM now
  ends at 0x0203e920 (before 0x0203eff8, limit 0x0203f000); IWRAM is
  unchanged at 0x03006de4.
- Tests: `tests/exfat/run.py` runs the library, folder, playlist (power cut)
  and layout host tests, and with `--acceptance` all exact-ROM acceptance, on
  exFAT cards, once with contiguous and once with fragmented files and
  folders. `tests/exfat/bin` stands in for mtools on exFAT images
  (mkfs.exfat formats, `exfat_tool` writes with FatFs, `exfat_read.py` reads
  independently), and fsck.exfat checks the card before every later look at
  it. `tests/test_exfat_layout.py` checks that both layouts really occur.
  On the fragmented card a playlist may open 2 frames later (gate 17 instead
  of 15 frames, `measure_playlists.py`): the playing song then needs a FAT
  lookup per 4 KB cluster. A FAT32 card fragmented the same way takes 24
  frames (EverDrive model); exFAT 16.

## Volume Boost, seeks, keys and memory (v2.2)

- `main.c` Volume Boost: a level change only sets the gain for the audio
  decoded after it, for GAC as for MP3. v0.9.5-v2.1 cut the GAC ring to
  6144 samples and decoded again from the playing sample (a seek in the
  file, an 8 KB read, skipped frames): deep into a long GAC that took up to
  90 ms per tap and underran (2 of 6 single taps at 4 minutes on a FAT16
  card with 4 KB clusters, SuperCard model; 4 of 6 on the EverDrive model;
  bursts of 3 taps on 512 B clusters underran 8 times in 8 bursts). While
  paused every tap restarted the song (0.45 s, only 4 of 12 taps counted).
  Now a change is heard once the buffered part has played: within about
  0.3 s (44151 Hz GAC) or 0.42 s (32768 Hz GAC, MP3 output). The noise
  shaping state carries on. `player.h`: `anchor_boost`, `boost_changes`
  and `boost_at[8]` log the level at the stream anchor and where each change
  starts (ring position << 2 | level); `anchor_pos` stays 0.
- `storage.c`: the GAC seek table is checked once per file instead of at
  every open (0.34 s for a 10 MB file, at every seek step and song start);
  the check is remembered with the path's seek index slot for that file
  size, first cluster and header (FNV-1a), and forgotten by
  `storage_close()` (before files are renamed, deleted or rewritten) and
  `storage_init()`. Opening the open song again (a seek, a restart) rewinds
  it instead of closing and reopening it.
- `storage.c`: FatFs fast seek for the playing song: a 128-entry cluster
  link map (512 bytes, up to 63 fragments, `CREATE_LINKMAP`). Making it
  walks the whole FAT chain (0.6 s for a 12 MB MP3 on 512 B clusters, with
  no key poll: a key was lost and a gapless song change could underrun),
  so it is made at the first seek that walks at least an eighth of the
  file, not at open. A more fragmented file (`FR_NOT_ENOUGH_CORE`, as on the
  fragmented exFAT test card) seeks the normal way. With the seek table no
  longer checked per step, a 10-second seek step 4 minutes into a 10 MB GAC
  (4 KB clusters) stops the main loop for 5-6 frames (most of it the
  4096-sample prefill) instead of up to 30, and held Left / Right steps
  every 18 frames instead of 38; without the link map the stop grew with
  the position (6 frames at 10 s, 10 at 4 minutes).
- `main.c`/`input.c` keys: the sound interrupt's key poll latches changes
  both ways (presses only before; the same instruction count, IWRAM
  unchanged), and `input_poll()` notes releases. A held key that went up and
  down again while the main loop was busy (a state save, a long decode
  step) is a release and a new press in the next frame (`input_moved()`);
  before, the second press was lost (18 of 24 quick taps counted with 50
  modeled SD busy polls). A key that went down and up between two frames
  still counts as held for one frame, as before; chords and consumed keys
  are unchanged.
- Memory: the Helix arena is 24 KB instead of 32 KB (the decoder allocates
  23848 bytes; the converter's MP3 decoder uses the same arena), and the MP3
  seek index keeps a 32-bit offset and a 16-bit point number (frame / 128,
  0xffff empty) instead of two 32-bit words (12344 instead of 16440 bytes
  for both slots). With the link map, EWRAM now ends at 0x0203bb70 (before
  0x0203e920, limit 0x0203f000); IWRAM is unchanged at 0x03006de4.
- Tests: `tests/check_ring.py` and the oracle (`check_experimental_dac.py`
  `Oracle.sequence(..., changes)`) model a change from its logged ring
  position on with the noise shaping error carried over (still byte for
  byte); `test_delivery_audio.py` gac-live-steps ends 12 frames after the
  last change so the compared ring holds both sides of it, and gates no
  restart (generation 1) and 6 logged changes. `tests/test_v22_input.c`
  (host) checked the key latching (undone in 11.0). `tests/test_v22_boost_stress_guest.py`
  (in `run_acceptance.py`): a 5-minute 44151 Hz GAC on FAT16 cards with
  4 KB and 512 B clusters: taps at 4 minutes after a held seek (ring equal
  to the oracle), bursts of 3 taps, 12 taps while paused, and taps during
  state saves with 50 busy polls; no underrun, at least 6144 samples
  buffered, every tap counted. The v2.1 ROM fails all four.

## Data folder visible on a PC (v2.2)

- `library.c` (`library_system_dir`): `/gbamp3/GBAMP3.SYS` is made without
  the hidden and system attributes (earlier versions set both when making it), so
  it shows on a PC. A folder that still has either attribute (an older card)
  is cleared with `f_chmod` at the first call, at start after mounting
  (`main.c`), and the library and saved state load from it as before. A
  failure (a write-protected card) is ignored. The player still writes only
  inside the folder (plus that attribute byte of its entry in `/gbamp3`).
- `folder.c` / `folder.h` (`folder_system`): the folder is left out of
  Folders (and the folder lists that use the same cursor: conversion
  targets) and the Update Library walk (`library.c`; Search uses the
  library) by its name in any case, no longer only because it is hidden.
  Other hidden or system entries and dot names are skipped as before. ROM
  code only: IWRAM still ends at 0x03006de4 (limit 0x03006e00), EWRAM at
  0x0203bb70.
- `README.md`, `CONTROLS.md`, `gbamp3-controls.pdf`: the folder is visible.
- Tests: `tests/test_v22_visible_sys_guest.py` (in `run_acceptance.py`,
  passed on the SuperCard, EZ-Flash Omega and EverDrive models): a fresh
  card's folder has neither attribute; on an older card made hidden +
  system with MP3s in the folder and in a sub-folder of it, both are cleared
  at start, the library and Playback Mode load, and Update Library (3
  songs) and Folders (2 entries in `/gbamp3`) leave the folder out; a folder
  named `gbamp3.sys` in lower case is used and left out the same way. The
  previous ROM fails the attribute and lower-case checks.

## exFAT folder growth and power cuts (v2.2)

- `ff.c` (`create_chain`, `dir_next`; marked "exFAT power-cut fix", the fix
  made and tested in gbawriter): on exFAT, a new fragment's FAT entry is
  marked end of chain before the previous cluster is linked to it, so a
  power cut never leaves a link to a stale FAT entry; and an exFAT
  sub-directory ends at its data length instead of following its FAT chain
  past it. Before, a power cut while a fragmented `GBAMP3.SYS` grew (a new
  playlist, state or library file) could leave the folder unable to grow
  again (FR_INT_ERR) and entries past its size, invisible on a PC. FAT code
  unchanged. ROM code only: IWRAM still ends at 0x03006de4 (limit
  0x03006e00), EWRAM at 0x0203bb70.
- Tests: `tests/test_exfat_folder_powercut.py` (in `tests/exfat/run.py`,
  contiguous and fragmented): `GBAMP3.SYS` filled to its end with the next
  cluster taken, a new playlist (and a song added) grows it into a new
  fragment, the power is cut after every sector write; afterwards
  fsck.exfat is clean (or finds only the folder one cluster longer than its
  size), FatFs lists the folder as `exfat_read.py` does, the playlists read,
  and 60 more files and a new playlist and state are added, clean and
  listed the same. Without the fix the fragmented run fails (FR_INT_ERR on
  the next file after the cut). Cuts between the two sector writes of an
  entry set that crosses a sector boundary (a wrong entry set checksum) are
  repaired at start since "exFAT torn entry sets repaired at start" below;
  the test now checks them like every other cut.

## v2.2: START only unlocks, also with a bouncing contact

- `player/src/ui.c`: after unlocking (lock screen or sleep), every button is ignored until all buttons have been up for 6 UI frames. A real START contact can bounce (down-up-down within a frame or two); the second press used to open Song Options or the screen's START action right after unlocking. 8 bytes of EWRAM, no IWRAM.
- `tests/test_v22_unlock_guest.py` (in `tests/run_acceptance.py`): bouncing START on release and on press, playing and paused (sleep), and START working again afterwards. It fails on the previous ROM.

## v2.2: exFAT torn entry sets repaired at start

- `player/src/exfat_repair.c` / `exfat_repair.h` (new; the module shared by
  the GBA apps, copied unchanged): FatFs writes an exFAT entry set (a file's
  directory entries) sector by sector; a power cut (or a failed write)
  between the two sectors of a set that crosses a sector boundary leaves a
  wrong set checksum. fsck.exfat reports it, and FatFs fails lookups in the
  folder with FR_INT_ERR (gbamp3's hash-skipping lookup often steps over
  the set instead, but the card stays damaged). `exfat_repair_dir` keeps
  the last complete state: a torn new set is freed, a torn rewrite gets its
  checksum fixed. It does nothing on FAT and writes nothing on a sound
  folder. ROM (Thumb), no static data, uses FatFs's sector buffer.
- `main.c`: at start, after mounting and before the library, playlists and
  state are read, `/`, `/gbamp3` and `GBAMP3.SYS` are repaired (writes
  allowed only for that). Before a song file is renamed or deleted, a folder
  is deleted, or a conversion writes `NAME.part` / `.blk` / `.sk` next to the
  song, that folder (and those above it) is repaired; a rename or delete
  that still fails with FR_INT_ERR is repaired and tried once more.
  `library.c` (`library_repair_dir`, `library_repair_folder_of`,
  `library_open`): the library, state, Play Next and playlist saves retry
  creating their file once after a repair on FR_INT_ERR, and so does the
  check of `GBAMP3.SYS` before each save. `storage.c`: `storage_fs()`.
- `ff.c` (`dir_register`, "exFAT fix"): a new entry set that takes two new
  clusters of its folder (512-byte clusters, a long name when the folder's
  last cluster is full) extends the folder's data length by both; FatFs
  R0.15 added one (the set's end past the data length: a fsck.exfat error,
  with the earlier `dir_next` fix FR_INT_ERR at once). FAT code unchanged.
- Memory: ROM code plus a 256-byte EWRAM path buffer. IWRAM still ends at
  0x03006de4 (limit 0x03006e00); EWRAM ends at 0x0203bca0 (before
  0x0203bba0, limit 0x0203f000).
- Tests: `tests/exfat_repair/` (in `run_host.py`; skipped without Linux or
  fsck.exfat): the module against this FatFs copy, 360 cases (create,
  append, rewrite, delete, rename, mkdir; sets of 3 to 19 entries across a
  sector boundary at every offset), a power cut after every sector write,
  fsck.exfat clean after each repair; it fails without the `dir_register`
  fix. Its torn-in-root case accepts gbamp3's lookup reporting the torn
  folder as missing (FR_NO_FILE) instead of FR_INT_ERR.
  `tests/test_exfat_folder_powercut.py`: the player starts again (with the
  repair) after every cut; cuts that tear an entry set must now pass like
  all others (fsck.exfat clean, listing equal to `exfat_read.py`, playlists
  read, the folder grows again), and there must be some.
  `tests/test_v22_exfat_torn_guest.py` (`tests/exfat/run.py --emu`, also
  with `--acceptance`; contiguous and fragmented cards): a power cut (on
  the host, in the player's own playlist code) tears an entry set in
  `GBAMP3.SYS`; the ROM starts with no fatal error, the library and the
  saved Playback Mode load, a Playback Mode change and Update Library save,
  fsck.exfat finds the card clean afterwards and the next start loads what
  was saved. The previous ROM leaves the torn set (fsck.exfat fails).
  `tests/exfat/bin/mattrib` (with `attr` / `chmod` in `exfat_tool`): a
  mattrib stand-in, so `test_v22_visible_sys_guest.py` runs on exFAT.

## v2.2: FatFs from gbaSD

- FatFs now comes from gbaSD (`gbasd/fatfs`, submodule 952664d -> 42a5e06;
  the drivers in `gbasd/src` and `gbasd/include` are unchanged). It is the
  FatFs the GBA apps share: R0.15 with the exFAT power-cut fixes made here
  and in the other apps, `exfat_repair.c` / `.h`, and gbamp3's extensions as
  switches. `player/vendor/fatfs/ffconf.h` sets them:
  `GBASD_FF_FAST_EXFAT_LOOKUP 1` (the hash-skipping exFAT lookup),
  `GBASD_FF_FSIZE32 1` (32-bit `FSIZE_t` with exFAT), `GBASD_FF_OPEN_ENTRY 1`
  (`f_open_entry` / `f_opendir_entry`, `FILINFO.fstat` / `fclust`, exFAT
  folder sizes). Removed: `player/vendor/fatfs/ff.c`, `ff.h`, `ffunicode.c`,
  `diskio.h`, `player/src/exfat_repair.c` / `.h`. `player/build.py` compiles
  `gbasd/fatfs/ff.c`, `ffunicode.c` and `exfat_repair.c` by name (the
  objects keep their names and link order); gbamp3 keeps its own
  `player/src/diskio.c` (it polls the audio during card access) and its
  `ffconf.h` (found first on the include path).
- New with it (gbaSD's `GBASD_FF_FAT_STRETCH_FIX`, on): on FAT12/16/32 a new
  folder cluster is cleared before the folder is linked to it. Before, a
  power cut while a folder grew (e.g. `GBAMP3.SYS` getting a new file) could
  leave it ending in a cluster of old file data: garbage names on a PC,
  fsck.fat errors. exFAT is unchanged.
- Same code otherwise: the ROM's objects are identical to the previous
  build's except `ff.o`, where only `create_chain` (now `create_chain_ex`),
  `dir_alloc`, `f_lseek`, `f_mkdir`, `f_write`, `store_xdir` (code laid out
  or inlined around the fix) and `create_chain`'s constant table differ, the
  set gbaSD's `tests/fatfs/equivalence.py` lists for gbamp3. ROM code only:
  every IWRAM and EWRAM symbol is at the same address (IWRAM ends at
  0x03006de4, limit 0x03006e00; EWRAM at 0x0203bca0).
- Tests: host builds compile `gbasd/fatfs`. `tests/exfat/run.py` uses
  gbaSD's exFAT test kit (`gbasd/tests/exfat_kit`: `exfat_tool` built with
  gbamp3's `ffconf.h`, its `bin/` stand-ins, `exfat_read.py`;
  `EXFAT_FRAGMENT_MODE=none` for gbamp3's fragmented cards); gbamp3's copies
  (`tests/exfat/exfat_tool.c`, `mtools.py`, `exfat_read.py`, `bin/`) are
  removed. `tests/exfat_repair/` is replaced by gbaSD's
  `tests/fatfs/exfat_repair` run with gbamp3's `ffconf.h` (it allows
  FR_NO_FILE for the fast lookup), and `run_host.py` also runs gbaSD's
  `tests/fatfs/growth_cut.py` with it (folder growth on FAT12/16/32 and
  exFAT, a power cut after every sector write).

## v2.2: Secret Settings > Hold START to unlock

- `player/src/ui.c`: a new Secret Settings switch, row 8 "Hold START to
  unlock: Off / On" (Off by default), before Delete configuration (now row
  9) and Delete library index (row 10). With it On, the lock screen unlocks
  only when START has been held 60 frames (1 s); a shorter press does
  nothing and the screen stays black. After unlocking, the guard of "START
  only unlocks" applies as before (the held START's release does nothing).
  Off: one START press unlocks, as before.
- `player/src/main.c` (`player_sleep`): asleep (locked, nothing playing),
  START wakes the GBA as before; with the switch On it unlocks only if START
  is still down 1 s later (the cycle timer; the screen stays off, the sound
  circuit off, no keypad interrupts meanwhile). Released earlier, the GBA
  goes straight back to Stop (each Stop counts in `stats.sleeps`).
  `player_hold_unlock` / `player_hold_unlock_toggle` (`player_api.h`).
- `player/src/state.c` / `state.h`: saved as bit 7 of the Secret Settings
  `flags` byte (older files have it clear: Off); Delete configuration resets
  it.
- ROM code only: every IWRAM and EWRAM symbol at the same address (IWRAM
  ends at 0x03006de4, limit 0x03006e00; EWRAM at 0x0203bca0); `ui_frame`'s
  stack frame unchanged (160 bytes).
- `CONTROLS.md`, `gbamp3-controls.pdf`: the switch, and the lock line.
- Tests: `tests/test_v22_hold_unlock_guest.py` (in `tests/run_acceptance.py`;
  run on the SuperCard, EZ-Flash Omega and EverDrive GBA models): Off, one
  short START unlocks (playing and asleep); the switch turned On in Secret
  Settings and loaded after a restart; On, playing and asleep: START held 30
  frames leaves it locked with the same screen (asleep: back to Stop, the
  Stop count goes up), still dark 30 frames into a 75-frame hold, unlocked
  once START has been down 60 frames, while it is still held (sampled per
  UI frame: 58 to 60 frames after the press in the runs), Now Playing (no
  Song Options),
  START works again afterwards. It fails on the previous ROM. The Secret
  Settings tests that open Delete configuration / Delete library index
  (`test_v13_guest.py`, `test_v13_delete_reboot_guest.py`,
  `test_v20_ui_guest.py`) take one more Down.

## v2.2: Hold START to unlock On by default

- Secret Settings > Hold START to unlock is now On by default, also on cards
  that saved a state with the previous build (where Off was the default and
  so is saved there): STATE0/1.DAT keep their layout and `GBAST003`, and the
  switch moves to a new bit with the opposite meaning, bit 1 of
  `new_playlist_top` (set = Off: one short START unlocks). Every older state
  has that bit clear and loads On; the previous bit 7 of `flags` is ignored
  and cleared on loading (`player/src/state.c` / `state.h`;
  `player_hold_unlock` / `player_hold_unlock_toggle` and
  `player_new_playlist_top` / `player_set_new_playlist_top` in
  `player/src/main.c` keep the two bits of that byte apart). Delete
  configuration resets it to On (no saved state). The Secret Settings row
  reads "Hold START to unlock: On" on a fresh card. Otherwise unchanged: On,
  START held 1 s unlocks the lock screen and a sleeping GBA, shorter does
  nothing (asleep: back to Stop); Off, one START press, as before; the
  unlock guard as before. Going back to an older ROM after switching it Off
  puts + New Playlist at the bottom there (that ROM resets the byte).
- ROM code only: every IWRAM and EWRAM symbol at the same address (IWRAM
  ends at 0x03006de4, limit 0x03006e00; EWRAM at 0x0203bca0); `ui_frame`'s
  stack frame unchanged (160 bytes).
- `CONTROLS.md`, `gbamp3-controls.pdf`: On (default) / Off, the lock line
  (same seven pages).
- Tests: `tests/test_v22_hold_unlock_guest.py`: a fresh card (no state) is
  On, playing and asleep (START held 30 frames stays locked, asleep back to
  Stop; 75 frames unlocks after 1 s, START works again); switched Off in
  Secret Settings, saved (bit 1 set, bit 7 clear) and, after a restart, one
  short START unlocks (playing and asleep); a state as the previous build
  wrote it (bit 1 clear, bit 7 clear; and Repeat All, to show it is the one
  loaded) is On (the row as on a fresh card, playing and asleep), also with
  bit 7 set. `tests/test_v22_unlock_guest.py` runs its bounce cases with the
  switch Off (switched once in Secret Settings, saved) and checks that a
  short bouncing START leaves the default On locked. Both fail on the
  previous ROM. `test_ui_guest.py` and `test_v19_guest.py` (sleep) unlock
  with START held 75 frames; `tests/run_ui.py` gains `secret_row`,
  `card_state`, `put_state`; the `test_v13_search.c` stub returns On.

## v2.2: gbaSD 9cc52f8 (FAT delete / rename order)

- Submodule `gbasd` 42a5e06 -> 9cc52f8. `gbasd/fatfs/ff.c` `dir_remove`
  (gbaSD's `GBASD_FF_FAT_REMOVE_ORDER_FIX`, on by default in `ff.h`;
  `ffconf.h` unchanged): on FAT12/16/32 a deleted or renamed file's 8.3
  short-name entry is marked deleted on the card before its long-name
  entries. Before, a power cut between the two sector writes of a long name
  over a sector boundary (deleting a song, renaming a song or folder, a
  playlist or library save replacing its file) could leave the file visible
  under its 8.3 alias (`ADIARY~1.TXT`); now a cut leaves at most orphaned
  long-name entries, which every reader skips. exFAT is unchanged. The same
  sector writes as before.
- Code: only `ff.o` differs. `dir_remove` is now a function of its own
  (924 bytes, ROM) instead of being inlined into `f_unlink` and `f_rename`
  (each 136 / 140 bytes smaller): ROM code +648 bytes. Every IWRAM and EWRAM
  symbol at the same address (IWRAM ends at 0x03006de4, limit 0x03006e00;
  EWRAM at 0x0203bca0).
- Tests: `run_host.py` also runs gbaSD's `tests/fatfs/remove_cut.py` with
  gbamp3's `ffconf.h` (delete, rename in a folder and to another folder of
  files with 22- and 250-character names at every offset in a sector, a
  power cut after every sector write, FAT12/16/32 and exFAT; no 8.3 alias,
  fsck clean).
