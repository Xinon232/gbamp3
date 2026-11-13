gbamp3 converter 3.2 — GAC Converter

No Python, FFmpeg installation, compiler, or normal terminal commands are needed.
This is a portable desktop application. Extract the ENTIRE archive before use;
keep the application and all its accompanying files together.

Windows x64: extract the ZIP, then double-click GAC-Converter.exe.
Linux x64: extract the .tar.gz with your archive manager, then open GAC-Converter.
  A graphical desktop is required. The hosted build uses Ubuntu 22.04; older
  systems are not qualified. Keep executable permissions when copying files.
macOS: choose the archive for your Mac: arm64 = Apple Silicon, x64 = Intel.
  Extract the ZIP, then open GAC-Converter.app (or move it into Applications).
  These native app bundles are ad-hoc signed, NOT Developer ID signed/notarized.
  macOS may block the first launch. Only if you trust the download, use the
  system's Privacy & Security > Open Anyway approval for this specific app.
  Do not disable system-wide security. The CI-tested OS version is recorded in
  BUILD-INFO.json; older macOS versions have not been qualified.

The Windows executable is unsigned and may trigger SmartScreen. Do not treat
these packages as signed/trusted installations or bypass unknown-file warnings
without checking the source and published SHA-256 first.

Use Add songs or Add folder. Switch direction beside MP3 → GAC to decode GAC
back to MP3. Choose an output option, then start. Defaults preserve originals.
Replacement always requires confirmation. Existing destinations are skipped.
The application saves preferences, not the song queue, in your user profile.
Recordings of hours convert in bounded memory (one GAC holds at most 18.2
hours at 32768 Hz, 13.5 hours at 44151 Hz); Cancel now stops the current song
without changing any file.
Advanced includes FFmpeg override and channel choices. The bundled FFmpeg is
used automatically unless you explicitly choose an override.

The native conversion helper and correct-architecture FFmpeg are bundled.
LICENSES contains project and runtime notices, exact FFmpeg build configuration,
source/binary provenance and offline backend rebuild instructions. Keep the
matching ffmpeg-source-TARGET.tar.gz source asset with any redistributed release.
BUILD-INFO.json and SHA256SUMS identify this build and package files.
VERIFICATION contains the actual frozen-binary GUI conversion smoke report.
No music is bundled; smoke tests create synthetic audio.

Pre-release limitations: native hosted-runner smoke is not physical GBA audio
qualification. The player ROM and controls PDF are separate unchanged assets;
this package does not modify the player. See NOTICES.md for inherited licensing
information. See LICENSES/THIRD-PARTY-SOURCES.txt before redistribution.
