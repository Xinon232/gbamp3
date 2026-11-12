GAC Converter for Windows x64 — gbamp3 12.0

1. Extract this ZIP fully (keep ffmpeg.exe next to GAC-Converter.exe).
   Run GAC-Converter.exe. Python, Tk, numpy and ffmpeg.exe are included;
   you do not need to install anything.
2. Add MP3 files, or "Add folder..." to convert a whole folder (with its
   subfolders when "Include subfolders" is ticked).
3. Choose where the .gac files go: next to each MP3 (default), or into an
   output folder (the subfolder structure is kept). An SD card works.
4. Optional: "Replace" deletes each MP3 after its .gac is saved and checked,
   so the card holds only .gac files. "Overwrite" replaces existing .gac files;
   without it they are skipped.
5. Rate: unchecked selects 32768 Hz. Select 44,150.6 Hz for the best stereo
   quality. Check Mono for files about 30% smaller that always play mono.
   Converting takes roughly a minute per four-minute song.
6. Convert, then copy the .gac files beneath /gbamp3 on your card.
   These are GAC v4 files: they need gbamp3 v0.9.0 or later, which plays only
   GAC v4. Older .gac files show OLD GAC - RECONVERT; convert them again from
   the MP3s.

MP3s are deleted only with Replace. The converter
stores title, artist, album, album artist, genre, track and disc (UTF-8) and
uses the picture embedded in each MP3 as its cover. A different cover image can
be chosen with the separate Python CLI (--cover), not this GUI.

This unsigned executable may trigger Windows SmartScreen.
The GitHub Windows build smoke-tests the packaged program: finding the bundled
ffmpeg.exe, a 44.15 kHz conversion, and a folder conversion with Replace. This is not exhaustive Windows testing or physical-GBA qualification.
See the bundled controls manual and notices. ffmpeg.exe is an unmodified FFmpeg
build from imageio-ffmpeg under its own license: LICENSES/FFmpeg-LICENSE.txt
states it, with the version and where to get its source.

Made by Halim Jarrar, (C) 2026. halim-jarrar.de / monday@halim-jarrar.de.
gbamp3 is free software, licensed under the GNU General Public License
version 3 or later, with the additional permission described in NOTICES.md.
No warranty. Source code: github.com/Xinon232/gbamp3
