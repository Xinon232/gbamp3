# Desktop converter core

The GBA player and GAC v4 wire format are unchanged. `gbacache.convert` retains
its positional API and adds an optional `progress(fraction, message)` argument.
`reverse_convert(src, dst, ffmpeg, quality=0, channels=None, overwrite=False,
replace=False, progress=None)` writes LAME VBR MP3 at 44100 Hz; V0 is the default,
and `None` preserves mono/stereo. Reverse conversion cannot restore information
lost in the GAC encode. Stored bounded metadata and RGB555 cover are transferred;
the cover becomes a PNG attached picture. PCM is saturated to signed16, not
quantized to the GBA's 8-bit output.

For unusually short caches (under 64 source frames), the FFmpeg resampler is
supplied a padded tail and trimmed to the desired duration. MP3 encoder/decoder
delay can impose a minimum 47 decoded samples. Ordinary output durations are
checked to one sample of the expected 44100-Hz resampling duration.

## Batch safety

Call `collect(inputs, recursive=False, output_dir=None,
direction='mp3_to_gac')` (or `direction='gac_to_mp3'`), then **call
`validate_pairs(pairs)` before converting any pair**. The existing forward CLI
also preflights the whole batch. Case-sensitive inputs remain distinct on Linux;
output collisions are checked case-insensitively for SD-card compatibility.
Canonical path aliases, hard links, duplicate destinations and outputs aliasing
any input are rejected.

Existing output files are skipped unless the API's explicit overwrite flag is
set. Output symlinks and replacement of source symlinks are rejected. A private,
randomly named same-directory temporary file is flushed, fsynced and validated
before atomic publication. No-clobber publication uses hard links where available,
Windows no-replace rename, or Linux/macOS exclusive rename on filesystems without
hard links. If neither safe primitive is available, conversion fails rather than
falling back to an overwriting POSIX rename. Unrelated `.part` files are untouched.

Forward audio decoding uses FFmpeg's strict error handling and rejects error-level
stderr even if FFmpeg exits zero. Incomplete PCM frames are rejected rather than
silently truncated. These failures retain the original and any existing output.
This detects reported decode errors, not every possible corruption or truncation
that still forms a valid decodable MP3 stream.

The final published path is verified again. Source and destination identities,
sizes and timestamps are rechecked before deleting the original. Failures and
skips keep the input; a deletion permission error leaves both the verified output
and the source. This is not a transaction against hostile concurrent directory
mutations or a guarantee against hardware/power failure. GAC has no audio-payload
checksum, so validation cannot detect every audio-bit change; it checks framing,
header fields, seek bounds, metadata validity/checksum and artwork checksum.

Callbacks are observational: exceptions are ignored so a closing GUI cannot
turn a completed conversion into an ambiguous failure. Notifications are monotone
and capped at 101 per file. FFmpeg stages report their phase; GAC encoding,
packing and reverse decoding report measured work fractions. Stop-after-current
is a batch/UI policy, not mid-file cancellation.

## Optional native helper

```
python tools/build_native.py --output-dir tools
```

Use `cc`/`CC` on Linux/macOS or an MSVC Developer Prompt on Windows. No compiler
is needed for source fallback or for a packaged application. The builder produces
`libgac_native.so`, `libgac_native.dylib` or `gac_native.dll`. The bridge only loads
adjacent/frozen-resource libraries, checks its ABI, and falls back to NumPy/Python
when a compatible helper is unavailable. `gac4.native_available()` reports the
actual loaded state. `GBACACHE_DISABLE_NATIVE=1` explicitly tests the fallback.

The C kernels use explicit unsigned wrapping and floor division, fixed-width
integers, no CPU-specific ISA, no fast-math and no mutable global codec state.
The native decoder returns unclipped signed32 reference PCM. The forward kernel
only replaces the closed-loop candidate sample loop; allocation of full-track
reconstruction diagnostics is omitted from `build` and `convert`.
`gac4.encode(..., diagnostics=True, native=False)` retains the reference API.

## Core verification commands

Run from the repository root with NumPy and FFmpeg installed:

```
python tools/build_native.py
python -m unittest discover -s tests -p test_converter_core.py -v
GBACACHE_REQUIRE_NATIVE=1 python -m unittest discover -s tests -p test_converter_native.py -v
GBACACHE_DISABLE_NATIVE=1 python -m unittest discover -s tests -p test_converter_core.py -v
python tests/test_gac4.py
python tests/test_metadata_storage.py
python tests/test_artwork.py
cc -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all tests/test_converter_native_sanitize.c -lm -o /your/scratch/native-sanitize
/your/scratch/native-sanitize
```

On PowerShell set `$env:GBACACHE_REQUIRE_NATIVE='1'` before the test command.
The sanitizer and inherited production-player tests above are Linux host tests;
the core/native unittest suites and native builder are intended for native
Windows/macOS CI too. A required-native run fails if only fallback is present.

`converter_v12_golden.json` records complete-file SHA256/length for seven
synthetic PCM fixtures × both rates × both channel layouts, including metadata
and cover. It was generated using unchanged `gac4.py` and `gbacache.py` from
`edff823f12d480cf8530b100eacca2d8cee6db33`, with both files checked against `git
show` before generation. Current reference and native paths must match all hashes.

For longer Linux-host full-conversion parity/performance measurement:

```
python tests/benchmark_converter_core.py --baseline /path/to/v1.2/tools --output /your/scratch/evidence
```

This creates only synthetic 60-second audio, runs fresh processes at both rates
and both layouts twice, compares full output hashes, and records wall time and
peak RSS. Results are host measurements, not a promise of uniform acceleration.
Native Windows/macOS execution and packaged GUI smoke remain separate release
qualification gates; a Linux core pass does not qualify those packages.
