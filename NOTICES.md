# Credits, licenses and source availability

**Halim Jarrar** — gbamp3 project, local adaptation, player and release integration.

## Licence

Additional permission under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or combining it with the Helix MP3 decoder (or a modified version of that library), containing parts covered by the terms of the RealNetworks Public Source License 1.0, the licensor of this Program grants you additional permission to convey the resulting work. Corresponding Source for a non-source form of such a combination shall include the source code for the parts of the Helix MP3 decoder used as well as that of the covered work.

Halim Jarrar grants the same additional permission for his files in `gbasd/`.

gbamp3 is free software, licensed under the GNU General Public License version 3 or later, with the additional permission above. The Helix MP3 decoder in player/vendor/arduino-libhelix/src/libhelix-mp3 is licensed under the RealNetworks Public Source License 1.0 (see LICENSES/RPSL.txt).

Copyright (C) 2026 Halim Jarrar. gbamp3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License (`LICENSES/GPL-3.0.txt`) for more details.

Source code: https://github.com/Xinon232/gbamp3

## Credits

**Helix DNA Client technology included. Copyright (c) RealNetworks, Inc., 1995-2002. All rights reserved.**

The modified Helix MP3 decoder and derived kernels keep the **RealNetworks Public Source License 1.0** in `player/vendor/arduino-libhelix/src/libhelix-mp3`. Read `LICENSES/RPSL.txt`, `LICENSES/LICENSE.txt`, the per-file notices and `MODIFICATIONS.md`. The alternative RCSL text from the upstream distribution is kept in `LICENSES/RCSL.txt`.

- Jon Recker / Ken Cooke — Helix MP3 decoder; RealNetworks — original Helix technology.
- Phil Schatzmann / arduino-libhelix — inherited integration. Unused Arduino GPL helper classes, AAC, examples and sample music are excluded from this source bundle and ROM.
- David Guillen Fandos — SuperFW SD driver, Copyright (C) 2024, **GPL-3.0-or-later**, imported via Halim Jarrar’s gbareader v1.3.0 at `723232ab364ea5771f7a905ab8f6c8d77a3a5f78`; since v2.2 it comes from gbaSD (`gbasd/src/supercard_*`, `gbasd/src/crc.c`), which carries gbamp3's changes. Read `LICENSES/GPL-3.0.txt`, retained per-file notices and `MODIFICATIONS.md`. The read path and scan-gated sector writer/CRC are linked; writer provenance is retained in driver-upstream.diff and source notices.
- Michael “Chishm” Chisholm and other original libgba SD contributors / Dave Murphy and devkitPro — historical baseline credits. Their BSD/LGPL notices remain for provenance; the old libgba driver and types are no longer build inputs. No SDK startup, console, font or runtime is linked.
- Gabriel Bouvigne (Shine), Pete Everett (fixed-point port), Romain Beauxis (toots/shine) and contributors — **shine** MP3 encoder, **LGPL-2.0** (GNU Library General Public License version 2), in `player/vendor/shine` from https://github.com/toots/shine at `ab5e3526b64af1a2eaa43aa6f441a7312e013519`, used for GAC -> MP3 conversion. Read `LICENSES/LGPL-2.0.txt` (also `player/vendor/shine/COPYING`) and `player/vendor/shine/MODIFICATIONS.md`. The complete source of the ROM, including the modified library, is in this repository and each release's source archive, so the library can be changed and the ROM relinked (`python3 player/build.py`).
- Halim Jarrar — gbaSD (`gbasd/`, git submodule, https://github.com/Xinon232/gbaSD): cart detection and the EZ-Flash Omega and EverDrive GBA SD drivers, **GPL-3.0-or-later**. The Omega driver is written after the EZ-Flash Omega and Omega DE kernels (github.com/ez-flash/omega-kernel, omega-de-kernel) and ezfo-disc_io by Felix Jones and ApacheThunder, all **Apache-2.0** (no file of those projects is included; `gbasd/docs/ezflash-omega/`). The SuperCard SD driver in gbaSD is the SuperFW driver below.
- krikzz — EverDrive GBA SD code (edio-gba `everdrive.c`, `disk.c`, github.com/krikzz/gba-ed-pub), Copyright (c) 2024 krikzz, **MIT**; ported and changed by Halim Jarrar as `gbasd/src/edgba_driver.c`, where krikzz's notice is kept.
- ChaN — FatFS R0.15 with patch1, copyright 2022. Original permissive notice retained in each applicable file and `LICENSES/FatFS.txt`. Imported from gbareader v1.3.0; since v2.2 it comes from gbaSD (`gbasd/fatfs`, shared by the GBA apps, with gbamp3's extensions as switches set in `player/vendor/fatfs/ffconf.h`; `exfat_repair.c` from there too); `ffconf.h` enables writable FAT with long UTF-8 names and fast seek; `ffunicode.c` is included unchanged. exFAT is enabled. Narrow local extensions (FILINFO.fclust, FILINFO.fstat, f_open_entry, f_opendir_entry) are listed in MODIFICATIONS.md.
- David Guillen Fandos — SuperFW font renderer, UTF-8 helpers and font data (UNSCII and GNU Unifont glyphs), **GPL-3.0-or-later**, in `player/vendor/superfw_font` (taken from GBAVocab); the bundled pack is a Latin/Greek/Cyrillic subset. Read `LICENSES/GPL-3.0.txt`.
- Halim Jarrar — GBAWriter input engine (`player/vendor/gbawriter`) and GBAVocab accent-folding table (`player/src/fold_table.h`), **GPL-3.0-or-later**.
- Ben Wiley and Damian Yerrick — GSM Player design/control references for releases up to v0.7.0. The v0.8.0 interface is new, iPod-inspired and independently authored; it contains no GSM Player UI, art or font code.
- devkitPro / devkitARM — external build tools. mGBA — external emulator dependency (MPL-2.0); FFmpeg — external fixture-generation/reference tool. Neither executable/library is redistributed in the release package.

gbamp3's own code (player, user interface, library, GAC codec, converter, build scripts and tests) is by Halim Jarrar under the licence above; the per-file notices name it. `host/alias.c` is Helix-derived and is under the RPSL. Synthetic test inputs and reference PCM are not music bundled in the ROM.

## Build tools

`gbafix` (devkitPro) writes the standard GBA ROM header. No warranty or hardware certification is provided.
