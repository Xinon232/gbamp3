# gbamp3 12.0

gbamp3 is a music player for the Game Boy Advance with an iPod-style
interface. It plays MP3 files directly, or optional PC-prepared `.gac` audio
caches, from the SD card of a flashcart: SuperCard SD, EZ-Flash Omega (also
the Definitive Edition; start the ROM from the SD card) or EverDrive GBA. The
cart is detected at start (hold L+R for the SuperCard driver). Cards can be
FAT32, FAT16 or exFAT, of every size up to 2 TB.

The GBA builds its own music library (artists, albums, genres, songs, album
covers), with playlists, search, Play Next, Volume Boost and conversion
between MP3 and GAC on the GBA. Controls: [CONTROLS.md](CONTROLS.md) and
[gbamp3-controls.pdf](gbamp3-controls.pdf). GAC caches and the PC
converter: [CACHE.md](CACHE.md), [GAC_FORMAT.md](GAC_FORMAT.md).

This repository holds the complete source code of gbamp3 12.0.

## Licence

Copyright (C) 2026 Halim Jarrar. halimj.itch.io / gba@halim-jarrar.de.

gbamp3 is free software, licensed under the GNU General Public License
version 3 or later (`LICENSES/GPL-3.0.txt`), with an additional permission
under GNU GPL version 3 section 7 for linking with the Helix MP3 decoder; the
text of the permission is in [NOTICES.md](NOTICES.md). The Helix MP3 decoder
in `player/vendor/arduino-libhelix/src/libhelix-mp3` is licensed under the
RealNetworks Public Source License 1.0 (`LICENSES/RPSL.txt`).

gbamp3 is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

## Credits

- Helix DNA Client technology included. Copyright (c) RealNetworks, Inc.,
  1995-2002. All rights reserved. Helix MP3 decoder by Jon Recker and Ken
  Cooke, via Phil Schatzmann's arduino-libhelix: RPSL 1.0.
- SuperFW SD card driver, text renderer and fonts (UNSCII, GNU Unifont):
  David Guillen Fandos, GPL-3.0-or-later.
- gbaSD (git submodule `gbasd`, https://github.com/Xinon232/gbaSD): cart
  detection, the EZ-Flash Omega and EverDrive GBA SD drivers (Halim Jarrar,
  GPL-3.0-or-later; the EverDrive code after edio-gba by krikzz, MIT; the
  Omega driver after the EZ-Flash Omega kernel, Apache-2.0), the SuperCard
  driver from SuperFW, and FatFs.
- FatFs: ChaN, BSD-style licence (`LICENSES/FatFS.txt`).
- GBAWriter input engine and GBAVocab accent folding: Halim Jarrar,
  GPL-3.0-or-later.
- shine MP3 encoder (GAC -> MP3 on the GBA): Gabriel Bouvigne, Pete Everett,
  Romain Beauxis and contributors, LGPL-2.0.
- Built with devkitPro / devkitARM.

Details, per-component notices and modification records:
[NOTICES.md](NOTICES.md), [MODIFICATIONS.md](MODIFICATIONS.md),
`LICENSES/`, `driver-upstream.diff`, `DRIVER_PROVENANCE.json`.

## Build the ROM

Requirements:

- devkitARM GCC 16.1.0 with its C++ compiler and devkitPro `gbafix`
  (the `devkitpro/devkitarm` container image
  `devkitpro/devkitarm@sha256:15b79ce75822c289538d8153da5fa7aafe5e6adc32ad8a575a197beca0f0761b`
  has both). devkitARM is expected at `/opt/devkitpro/devkitARM` (override
  with the `DEVKITARM` environment variable) and gbafix at
  `/opt/devkitpro/tools/bin/gbafix`.
- Python 3.11 or newer (no extra packages for the ROM).
- git.

The SD card code is the gbaSD submodule, so clone with submodules:

```sh
git clone --recursive https://github.com/Xinon232/gbamp3.git
cd gbamp3
```

(or, in an existing clone, `git submodule update --init`). Then build:

```sh
python3 -B player/build.py
```

The ROM is `player/build/player.gba` (copy it to the card as `gbamp3.gba`).
Intermediate files go to `player/build/`, `player/evidence/`, `tmp/` and
`cache/`. A build of this tag with the toolchain above gives the ROM of the
gbamp3 12.0 release byte for byte.

The generated tables are committed; their generators are in `tools/`:
`tools/gac4.py` (GAC v4 decoder and encoder tables, needs numpy),
`tools/boost.py`, `tools/resample_tables.py` (needs numpy),
`tools/shine_tables.c` (shine's start-up tables) and `tools/font_subset.py`
(the font pack subset).

## PC converter

`tools/gbacache_gui.py` (window) and `tools/gbacache.py` (command line)
convert MP3 files to GAC caches; the window also converts GAC back to MP3.
They need Python 3.11+, numpy, FFmpeg and, for the window, Tkinter:

```sh
python3 tools/gbacache_gui.py
```

See [CACHE.md](CACHE.md) and [tools/CONVERTER_CORE.md](tools/CONVERTER_CORE.md).
`packaging/windows` and `packaging/desktop` hold the scripts that build the
standalone converter packages (PyInstaller; FFmpeg is downloaded or built
from source by those scripts, see `packaging/desktop/README.md`).
