# Minimal converter backend: editable offline source and relinking

This is a native FFmpeg 7.1.5 + LAME 3.100 + zlib 1.3.2 build. No patches are
applied. Only LAME and zlib are external codec/compression libraries. No network,
GPL, version3, nonfree, or external autodetection. This is not general-purpose
FFmpeg: MP3, bounded WAV/PCM, PNG/JPEG artwork, and converter filters only.

## Contents of each ffmpeg-source-TARGET.tar.gz

- `archives/`: exact upstream source bytes; SHA-256 and URLs in `recipe/ffmpeg-lock.json`.
- `recipe/`: complete editable orchestration, build script, verification and this document.
- `build-state/`: entire configured source/build trees, static libraries and objects,
  dependency prefix, compiler inventory, full command/build logs, generated configs.
- `source-notices/`: upstream top-level licenses, GCC runtime exception and JPEG-related
  notices; Windows additionally copies the actual UCRT64 toolchain package licenses.
  The checked-in MinGW text is a reference; the native toolchain's notices govern. All per-file
  notices are preserved in the full source archives and build trees. Compiler
  runtime notices and their provenance are included as well (native Windows
  toolchain installed notices supplement the pinned GCC/MinGW license texts).
- `qualification/`: actual codec/filter checks and executable hash.
- `ffmpeg-provenance.json`: source and recipe hashes, native architecture, executable hash.
  The app's copy additionally binds the final source-bundle hash (no circular self-hash).

## Offline clean rebuild (no network needed)

Install native developer prerequisites first: Python 3.11.8+ (stdlib only), bash,
make, C compiler, binutils and pkg-config. Linux: GCC; Mac: Xcode CLI clang tools;
Windows: MSYS2 UCRT64 GCC/make/pkgconf (not MSYS-linked GCC). These are development
prerequisites, never requirements for the end-user application. Toolchain/SDK
versions used for the original build are in `build-state/logs/toolchain.txt`.
Compiler/OS system components are not a bundled general compiler distribution.

Extract the source bundle to a path **without spaces**, then from its root:

```sh
python recipe/bundle_ffmpeg.py --offline --archives archives --output-dir rebuilt
```

On Windows use normal Windows Python 3.11 and set `FFMPEG_BASH` to the absolute
`.../msys64/usr/bin/bash.exe`. The script selects the UCRT64 login environment;
MSVC is used only for the separate application's native GAC helper. macOS defaults
to deployment target 14.0 for both architectures; only CI-tested systems qualify.
`BUILD_JOBS=2` is the default. No downloaded toolchain or network fetch is done
by the offline command. A new build-state is always required; stale binaries
cannot qualify a new build. Archive hashes are checked before extraction.

For modifications, first unpack/build as above. Edit `rebuilt/build-state/ffmpeg`,
`lame`, or `zlib` as desired, then run:

```sh
bash recipe/backend-build.sh "$PWD/rebuilt/build-state"
```

This reconfigures to the current absolute prefix and invokes make/install for
LAME/zlib then relinks FFmpeg. Existing objects/static archives are retained;
`touch rebuilt/build-state/ffmpeg/fftools/ffmpeg.c` forces a main-object rebuild
and link when experimenting with replacement libraries. On Windows execute this
command in UCRT64. For a fully clean modified rebuild use make clean in each
configured tree before the script. Do not relabel modified output as the pinned
unmodified release. Re-run backend_checks.qualify and the actual frozen GUI smoke.

Original configured makefiles reference the original absolute build prefix. To
use the retained original objects directly, restore that path (recorded in config
and logs), or rerun the script at the new path before relinking. Reproducible
bit-for-bit output across different toolchains/paths is NOT claimed.

The separately shipped standalone app archive includes the backend and notices;
the release must retain this source bundle beside it. No source offer or future
availability promise substitutes for these real bytes. See project notices.
