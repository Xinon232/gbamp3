# Desktop converter packaging

Builds standalone converter release candidates only. No player edits, remote push,
tag, release publication or repository-visibility change is performed here.

## Native targets

- Linux x64: `ubuntu-22.04`, GCC, PyInstaller onedir tar.gz.
- Windows x64: `windows-2022`, **MSYS2 UCRT64 GCC for FFmpeg/LAME/zlib** and the
  existing **MSVC helper build**, PyInstaller onedir ZIP. UCRT is an OS dependency.
- macOS Intel: `macos-15-intel`; ARM64: `macos-14`. Separate native clang builds,
  deployment target 14.0, architecture checked in real binary headers. App ZIPs
  are ad-hoc signed, **not notarized or Developer ID signed**.

Only actual native CI passes qualify those targets. A newer local Linux build
cannot establish Ubuntu 22.04 compatibility. Windows is unsigned. Hosted image,
SDK/compiler and package versions are recorded, not claimed immutable. Python
3.11 patch version is selected by setup-python; freezer requirements and action
revisions are pinned. MSYS2 package versions are recorded via `pacman -Q`.

## Source-built backend, no wheel or system fallback

`ffmpeg-lock.json` pins exact FFmpeg **7.1.5**, LAME **3.100**, zlib **1.3.2** source
archives with calculated SHA-256. FFmpeg's official download page and release
index were checked on 2026-09-28: 7.1.5 is the maintained 7.1-branch release dated
2026-06-20, not a claim that 7.1 is the newest major branch. The zlib homepage
independently publishes the pinned 1.3.2 checksum. The build uses no source patches.

The small allowlist supports MP3/V0 encoding, decoding, bounded WAV/PCM, PNG/JPEG
attached artwork, resampling, padding/scaling and metadata. It intentionally does
not promise AAC/FLAC/Opus/general-purpose video input. No external-library
autodetection, network, GPL/version3/nonfree dependencies. The binary's actual
license output must identify LGPL 2.1 or later. Import checks reject non-system
libraries (including accidental dynamic LAME/zlib or MSYS/GCC runtime DLLs).
No imageio package, wrapper license, prebuilt backend, or PATH fallback is used.

Every native build creates a real `ffmpeg-source-TARGET.tar.gz` beside the app
archive, containing **all three original source archives**, the editable build
recipe, complete configured build trees and objects/static archives, logs,
toolchain inventory, generated configs, license notices, qualification and source
provenance. The recipe is network-independent when using the included archives.
See **BACKEND-SOURCE.md** for offline rebuild and relink commands. A future source
offer is not substituted for these bytes. Publish the source bundle alongside
the matching application and retain it after temporary CI artifact expiration.

Official references:
- https://ffmpeg.org/download.html
- https://ffmpeg.org/releases/
- https://ffmpeg.org/platform.html
- https://zlib.net/
- https://www.msys2.org/docs/environments/
- https://docs.github.com/actions/reference/runners/github-hosted-runners

## Build and verify

Merge core/GUI first. Required shared interfaces are `tools/build_native.py`,
`gac4.native_available()`, `gbacache.py`, `gbacache_gui.py`, `converter_settings.py`,
and `converter_smoke.py`. Native helper filenames are `libgac_native.so`,
`gac_native.dll`, `libgac_native.dylib`. Python fallback cannot qualify a release.

Use Python 3.11 with Tk and native developer tools described in BACKEND-SOURCE.md.
For Windows, configure an x64 MSVC Developer environment and set `FFMPEG_BASH` to
MSYS2's absolute `usr/bin/bash.exe`; only the backend subprocess uses UCRT64.
On Linux install GCC/make/pkg-config, xvfb and xauth. Build paths need no spaces.

```sh
python -m pip install -r packaging/desktop/requirements.txt
python -m unittest discover -s packaging/desktop -p 'test_*.py' -v
python packaging/desktop/bundle_ffmpeg.py --output-dir backend-build
python packaging/desktop/build.py --backend-dir backend-build --output-dir desktop-build
python packaging/desktop/smoke_package.py --build-dir desktop-build --timeout 600
python packaging/desktop/archive.py --build-dir desktop-build
python packaging/desktop/verify_archive.py --build-dir desktop-build
```

On headless Linux prefix build.py, smoke_package.py and verify_archive.py with
`xvfb-run -a`. Use fresh output directories. Omitting --backend-dir builds sources
inside the app build directory; a supplied backend is accepted only when native,
qualified and its executable/source-bundle/recipe hashes match. `--source-root`
may select another integrated checkout, whose tools hashes/dirty state are recorded.

`bundle_ffmpeg.py --offline --archives PATH --output-dir NEW_PATH` needs no network.
This offline guarantee covers the backend, not provisioning a compiler/SDK or the
Python/Tcl/Tk/freezer dependencies and notices for rebuilding the entire GUI app.

## Release gates and artifacts

The workflow `.github/workflows/converter-desktop.yml` runs four native matrix
legs on the release branch or workflow_dispatch; it does not publish releases.

1. Hash-check sources, configure from an empty private prefix, compile and test
   actual FFmpeg codec/filter/metadata/artwork operations and dynamic imports.
2. Build the real native helper, probe a real Tk window, freeze the integrated app.
3. Run the **actual frozen executable** outside the checkout with Python/FFmpeg
   PATH and library overrides removed. Require fresh success JSON, exit 0,
   frozen execution and native acceleration, and bundled-backend identity.
4. Archive only the exact tested launcher. Require the source bundle's matching
   hash. `artifacts/SHA256SUMS` covers BOTH app and source archive.
5. Extract the app archive, validate all per-file hashes, and run the real frozen
   GUI forward/reverse/settings/safety smoke again. Never substitute mocked tests.
6. Upload both release-candidate archives, not only diagnostic evidence. Parent
   must download/check all four targets before publication.

Runtime notices preserve exact source licenses, compiler runtime notices and
actual FFmpeg `-L`/`-buildconf`; Python/Tcl/Tk/NumPy/PyInstaller/vendor notices remain.
No copyrighted music is bundled.

The legacy Windows workflow is unchanged; this four-platform source-backend
workflow supersedes it for release qualification. Do not publish an old wheel
backend merely because a legacy job is green.

## Freezer pitfalls retained

- PyInstaller 6.16/setuptools 79 on Python 3.11 needs explicit `backports.tarfile`
  collection for vendored jaraco.context.
- Standalone CPython/uv may resolve Tcl/Tk through executable RUNPATH. Analysis
  exposes the exact Python prefix lib; smoke clears the override to prove bundling.
