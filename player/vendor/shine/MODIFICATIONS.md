# shine in gbamp3

shine is a fixed-point MP3 (MPEG-1 Layer III) encoder. gbamp3 uses it to
convert GAC songs back to MP3 on the Game Boy Advance (v2.0, Secret Settings >
Conversion: GAC>MP3).

- Upstream: https://github.com/toots/shine, commit
  ab5e3526b64af1a2eaa43aa6f441a7312e013519 (`src/lib`).
- License: GNU Library General Public License version 2 (see `COPYING`). The
  files in this directory stay under that license; gbamp3's own code is
  GPL-3.0-or-later with the additional permission in NOTICES.md.
  The complete source of the ROM (this library with the modifications below,
  and everything it is linked with) is in this repository, so the encoder can
  be changed and the ROM rebuilt (`python3 player/build.py`).
- Not included from upstream: the command-line front end, the MIPS
  multiplication header, build files.

## Modifications (all marked "2026-10-07 (gbamp3)" in the sources)

- **No C library except string.h.** The start-up tables shine computed with
  `sin`, `cos`, `pow`, `sqrt` (MDCT window and cosines, analysis filter,
  quantizer step sizes, x^(3/4) table) are generated on the PC by
  `tools/shine_tables.c` into `shine_gen_tables.h`, with the same
  expressions. `tests/test_convert_gac.py` checks the file is current.
- **No malloc.** `shine_initialise(config, mem)` uses the caller's memory; the
  bit stream buffer is part of the encoder state.
- **No floating point while encoding:** the energy logarithms of the
  scale-factor selection information are integer (`ilog2_m21`, the same
  truncated values), x^(3/4) beyond the table is an integer root, the slot
  arithmetic is Q16, and the unused psychoacoustic inputs (always 0) are
  integers.
- **Speed (the GBA has 16.8 MHz):** the analysis matrix is folded by its
  symmetries to 32 columns (the anti-symmetric half uses the mean of each
  coefficient pair, which differ by at most 5 / 2^31 because upstream's PI64
  is rounded); the MDCT applies the window first and is folded to 18 x 18; the
  window and matrix run as ARM code (`player/src/mp3enc_arm.S`, same integer
  results); the quantized values of the current granule are kept in fast
  memory during the step size search; large local arrays are static.
- **Placement:** `SHINE_TABLE` puts the filter, window, MDCT and Huffman
  length tables, and `build.py` the hot code, into an IWRAM overlay.
- **Portability fixes:** `__asm__` instead of `asm` (ISO C), early-clobber
  outputs in the ARM multiply macros (the ARM7TDMI requires RdLo, RdHi and Rm
  to differ), byte stores instead of an unaligned word store, no shift by 32
  for a zero-length write; the portable multiply-accumulate macros keep a
  64-bit sum like SMLAL, so the PC build gives the GBA's output bit for bit.
- The bit stream readers of the side information read it in place instead of
  copying it to the stack.
