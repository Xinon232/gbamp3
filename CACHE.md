# GAC converter and cache playback

GAC is an optional PC-prepared audio cache. Original MP3 playback remains available; conversion is not mandatory. Put `.MP3` and `.GAC` files and folders under `/gbamp3` at the SD-card root. The GBA only reads them; it never converts or writes audio files (it writes only its own library, playlist and settings files in `/gbamp3/GBAMP3.SYS`).

## Install the converter

The converter requires **Python 3.11 or newer**, **numpy**, **FFmpeg**, and (for the windowed interface) **Tkinter/Tk**. FFmpeg and Python are external dependencies, not bundled executables. The Windows executable bundles Python, Tk and numpy.

Linux (Debian/Ubuntu/Mint): install `python3`, `python3-tk` and `ffmpeg` using your distribution's package manager. For example:

```sh
sudo apt install python3 python3-tk python3-numpy ffmpeg
python3 tools/gbacache_gui.py
```

Windows: the simplest route is `GAC-Converter.exe` from the release ZIP; it includes `ffmpeg.exe` and finds it automatically. From source: install Python with Tcl/Tk enabled, `py -m pip install numpy`, install FFmpeg, and put FFmpeg's `bin` directory on PATH (or select `ffmpeg.exe` or its folder in the converter). Extract the complete converter package before running:

```bat
py tools\gbacache_gui.py
```

The source is portable Python; the release verification lists what was executed on Windows.

## Choose the rate and layout

Leave the rate checkbox unchecked for **32768 samples/second/channel**, the normal cache rate. **44,150.6 Hz** gives the best stereo quality: it passes the same emulator checks as 32768 Hz and needs about 64% CPU on dense stereo music. The 65.536 kHz rate of earlier versions was removed in v0.9.0. An underrun pauses playback briefly and resumes; the player does not silently reduce the rate.

44,150.6 Hz uses the 65.536 kHz GBA output-stage setting (8-bit PWM); 32768 Hz uses 9-bit PWM, but DirectSound stereo delivers 8-bit samples to each side either way. The 44,150.6 Hz stream uses a fixed 380-cycle timer, actually 44150.568421 samples/second; the file stores nominal 44151, an approximately 10-ppm slower playback difference. Higher numeric rate does not restore frequencies missing from the original MP3.

**Mono** (checkbox, or `--mono`) stores one mid channel: files are about 30% smaller than stereo GAC and play mono in both sound modes. Stereo files can still be played in Mono on the GBA; that plays their mid channel and skips side decoding.

In the converter, add MP3 files or a whole folder (**Add folder...**; subfolders are included unless you untick it). Each `.gac` is saved **next to its MP3** by default, or into an output folder that keeps the subfolder structure. Output filenames retain each source stem and use `.gac`. Existing `.gac` files are skipped unless **Overwrite** is ticked. **Replace** deletes each MP3 after its `.gac` has been saved and checked (the converter asks first; a song that fails keeps its MP3), so a card can hold only `.gac` files. Without Replace, originals remain unchanged. Output can go straight to an SD card (FAT32/exFAT): files appear as `name.gac.part` while being written. For a rate or layout comparison, use separate output folders: the conversions otherwise have the same filename. Long file names (UTF-8) are fine on the card.

Long recordings (hours) convert with the same, small amount of memory (converter 3.2). One GAC holds at most 2^31 - 1 frames, 18.2 hours at 32768 Hz or 13.5 hours at 44151 Hz, and about 1 GiB of audio (reached first only by dense, noise-like stereo); a longer recording stops with "too long for gbamp3: about N hours max at this rate": split it into shorter MP3s. **Cancel now** stops the song being converted without changing any file.

## Command line

```sh
# Default 32768 Hz, next to the original; an existing SONG.gac is skipped.
python3 tools/gbacache.py SONG.mp3

# Multiple inputs or a folder (its MP3s only); separate destination.
python3 tools/gbacache.py ALBUM -o converted-32k

# A folder and all subfolders, .gac next to each MP3, deleting each MP3
# after its GAC is verified (--overwrite also replaces existing .gac files).
python3 tools/gbacache.py MUSIC -r --replace

# 44,150.6 Hz hardware rate (nominal integer 44151 in file): best stereo.
python3 tools/gbacache.py SONG.mp3 --rate 44151 -o converted-44k

# Mono layout, about 30% smaller.
python3 tools/gbacache.py SONG.mp3 --mono -o converted-mono

# FFmpeg outside PATH.
python3 tools/gbacache.py SONG.mp3 --ffmpeg /path/to/ffmpeg
```

FFmpeg decodes/resamples to signed 16-bit stereo on the PC. The converter encodes independently restartable GAC v4 blocks (adaptive LPC with Huffman-coded residuals, see `GAC_FORMAT.md`); the GBA decodes those blocks during playback. It is not decoding the MP3 again. GAC v4 files are about 30% smaller than v3 at equal or better measured quality (stereo music at 44,150.6 Hz: about 3.7 bits per frame, 20 KB/s). Conversion runs at roughly 4-5x realtime on a typical PC (a four-minute song takes about a minute). The player accepts only the nominal 32768 and 44151 Hz rates.

## Tags and cover

The converter embeds the MP3's tags in a GACMETA3 block: title, artist, album, album artist and genre as UTF-8 (accents and other scripts kept; the GBA shows Latin, Greek and Cyrillic), plus track and disc numbers with their totals. It reads the tags using FFmpeg and falls back to the input stem for a missing title. Fields are cut on a character boundary at 127/95/95/95/63 bytes. Original MP3 bytes remain unchanged.

**Cover:** by default the converter uses the picture embedded in each MP3, if it has one. To use a different image for all inputs of one command:

```sh
python3 tools/gbacache.py SONG.mp3 --cover album.jpg -o converted
```

FFmpeg prepares a fitted, black-padded 64x64 RGB555 image (v1.8; converters before made 32x32, which the player still shows, doubled). v2.1: Update Library reads this bounded thumbnail into the library, reduced to at most 240 colours, as the cover of the song's album (a GAC song without an album shows none); Now Playing shows it from there. It never decodes the GAC's source image. MP3 files get their album cover from the library (Update Library decodes an embedded JPEG). Failed explicit image extraction produces no output file.

**GAC v4 needs gbamp3 v0.9.0 or later**, and v0.9.0 plays only GAC v4. Older `.gac` files (v1-v3, made by converters before v0.9.0) show **OLD GAC - RECONVERT** and are listed by file name: convert them again from the original MP3s. If you used **Replace** with an older converter, the MP3s are gone; those songs cannot be recovered from the old `.gac` files.

Only song files are prepared on the PC. The GBA builds its library itself: Settings > Update Library. See CONTROLS.md.

## Playback on the player

Settings > Output selects **Stereo** or **Mono** for MP3 and GAC alike (one setting, remembered on the card). GAC output uses per-channel noise shaping to 8 bits; Mono plays the stored mid channel. Changing the setting restarts decoding at the current whole-second position and keeps the pause state.

Lo-fi processing has been removed completely from MP3 and GAC playback. Conversion is also unfiltered apart from FFmpeg's normal sample-rate conversion. Physical-hardware performance and listening review remain pending.
