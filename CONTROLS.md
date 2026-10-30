# gbamp3 - full controls

Made by Halim Jarrar, (C) 2026. halimj.itch.io / gba@halim-jarrar.de.

gbamp3 11.0: an iPod-style music player for the Game Boy Advance. MP3 and GAC v4 caches, a music library built on the GBA itself with album covers, playlists, search, one Stereo/Mono setting, an optional Volume Boost, and conversion between MP3 and GAC on the GBA. Hardware testing pending.

Put MP3 files, .gac caches and folders beneath /gbamp3 on the SD card. MP3 needs no conversion. GAC caches are prepared on a PC with the supplied converter (see CACHE.md). The GBA changes song files only when you ask it to (Rename, Delete or Convert, all Off unless switched on in Secret Settings); otherwise it writes only its own library, playlist and settings files in its folder /gbamp3/GBAMP3.SYS (visible on a PC; Folders and Update Library leave it out). No demo music is embedded.

## Setup and supported files

- Copy gbamp3.gba to the card and start it from your flashcart: SuperCard SD, EZ-Flash Omega (from the SD card, not NOR) or EverDrive GBA, detected at start (hold L+R: SuperCard driver). Not yet tested on hardware.
- MP3: MPEG-1, MPEG-2 or MPEG-2.5 Layer III, 8-48 kHz, CBR/VBR, mono or stereo. GAC: version 4 caches (stereo or mono layout) at 32768 or 44150.6 (nominal 44151) samples per second. Older GAC versions show OLD GAC - RECONVERT: convert them again from the MP3.
- Storage: SD card formatted FAT16, FAT32 or exFAT, of any size up to 2 TB.
- Long file and folder names (UTF-8) are supported. Paths may be up to 255 bytes. Latin, Greek and Cyrillic letters are shown; other scripts show a placeholder.
- Tags: ID3v2.2/2.3/2.4 (all text encodings) and ID3v1 are read on the GBA; GAC files carry their tags from the converter. A missing title shows the filename without its extension; missing artist, album or genre show Unknown Artist, Unknown Album or Unknown Genre.
- MP3 covers: Update Library reads the embedded front cover (ID3 APIC/PIC, JPEG) of the first song of each album that has one and shows it for every MP3 of that album. Baseline JPEG up to 256 KB and 1600x1600 pixels; PNG, progressive JPEG and larger pictures are skipped (no cover). Songs without an album tag show no cover. GAC songs use their album's cover too. Covers: 64x64 pixels, up to 240 colours. After updating to v2.1, run Update Library once.

## Buttons everywhere

- Up / Down: move the selection (hold to repeat). A fresh press wraps: Up on the first entry goes to the last (on Home: from Now Playing to Songs), Down on the last goes to the first. Holding Up / Down stops at the ends.
- A: open the selected entry, or play the selected song. A acts when released. A on the song with the play mark (the one that is playing or paused) opens Now Playing and continues it instead of starting it again.
- B: back one screen. B on Home does nothing.
- START: Song Options for the song under the cursor, in any song list (Songs, an album, a genre, a folder, a playlist, search results); in Now Playing, for the song that is playing. On the playlist list START opens Playlist Options; on Artists, Albums, Genres (and an artist's albums) it opens the options of that artist, album or genre; on a folder in Folders, Folder Options. START does nothing on other screens.
- SELECT: Settings, from any screen except Search and naming a playlist (there SELECT types).
- L+R together: lock. The screen goes black, music keeps playing and every button is ignored until START is held 1 s (pressed once with Secret Settings' Hold START to unlock Off), which returns to the same screen; that START press does nothing else, and buttons stay inactive until all are released. Lock is not available while typing in Search or a name.
- Locked with no music playing (paused or stopped), the GBA sleeps: it uses almost no power until START wakes and unlocks it. While music plays, the GBA rests between audio chunks to save power.
- Playback continues while you browse: leaving a folder, going back to Home or opening another playlist does not stop the music.
- With Secret Settings' Reorder menus switch On, hold A and press Up / Down to exchange adjacent rows on Home, Settings, Song Options and Playlist Options. Each of those four orders is remembered independently; Off (the default) disables only those four menus. Playlist and playlist-song A+Up/Down movement always works, even with the switch Off.
- With the switch On, A+Up on + New Playlist (last row of Playlists) puts it at the very top, A+Down puts it back at the very bottom; it never goes between playlists. Add to Playlist follows the same position. + Add Songs (last row of every playlist) moves the same way, on its own: its position is the same in every playlist.

## Home

- Initially: Now Playing, Playlists, Folders, Search, Artists, Albums, Genres, Songs. With menu reordering enabled, their positions can change without changing their actions.
- Folders shows the card as it is, with literal file names, folders first, then songs, each alphabetical. It works without a library.
- Artists, Albums, Genres, Songs and Search use the library. Before the first Update Library they show No music found.
- Artists opens an artist's All Songs or Albums. Albums lists songs by disc and track number. Genres lists songs by title.
- Sorting is alphabetical, ignoring case and accents (as in GBAVocab). Unknown entries sort last.
- A song list shows a play mark on the song that is playing. Long names scroll on the selected row.

## Long lists

Songs, Artists, Albums, Genres, an artist's or album's list, Folders, playlists and the playlist list (short menus such as Home and Settings stay as above):
- Holding Up / Down repeats from 0.4 s at 7.5 rows per second, the same with any music: the fastest the heaviest MP3 allows while the music keeps its full buffer. The screen shows every step whole (no half-moved rows).
- Left / Right: the list moves one page (8 rows) back / forward and the cursor stays on its row of the screen; holding repeats (2 pages per second). On the first / last page they do nothing.
- Alphabetical lists (Songs, a genre's songs, Artists, Albums, an artist's albums, Folders): hold L and press Up / Down (or hold them: 2 letters per second) to jump to the first entry of the previous / next letter, shown on the top row (near the end of the list as high as the list allows). Letters without entries are skipped; accented letters count as the base letter, digits and symbols as #, Unknown entries as ?. A fresh press on the first / last letter wraps. L + Left / Right and L or R alone do nothing; L + R still locks.
- Letter box (Secret Settings > Letter Box; Off by default): a dark box in the middle of the list shows the letter. On: while L is held, while Up / Down has been held for 1.5 s or pages repeat, and for 0.6 s after a wrap. Only L+DPAD: only while L is held and Up / Down steps by letter. It fades out (0.2 s) 0.6 s after fast movement stops, and when L is released or after a single step. The letter steps work with every setting.
- With Show songs as: File Names (Settings), song lists show file names without .mp3 / .gac, and the Songs list is in file-name order (letters follow the file names). Folders always show file names; Now Playing, Song Info and Search's fields are unchanged.

## Now Playing

- Shows a 64x64 cover (the album cover from the library; a grey note otherwise), title, artist, album, a progress bar when the length is known, elapsed and total time, the play/pause state, the playback mode and a technical row: format, the real output rate, Stereo/Mono and, above 0 dB, the Volume Boost level (+3dB, +6dB or +9dB).
- A: play / pause. The icon shows what A does: pause bars while music plays, a play triangle while paused or stopped.
- Left / Right tap: previous / next song. Previous restarts the song when more than 3 seconds have played, and restarts the first song of the list. Right on the last song wraps to the first.
- Left / Right hold: seek 10 seconds back / forward after half a second, then every 0.3 seconds while held.
- Up / Down (Volume Boost on in Settings): raise / lower the boost one step per press: 0, +3, +6, +9 dB. Holding does not repeat and the level stops at 0 and +9 dB. With Volume Boost off, Up / Down do nothing here. The new level applies to newly decoded music: heard within about 0.4 s for GAC, 1 s for MP3 (paused: after resuming).
- START: Song Options. SELECT: Settings (Playback Mode is its first row). B: back.
- With nothing selected the screen reads No song selected.

## Playback modes and the queue

- The list you start a song from becomes the queue: a song list, an album, a genre, a folder (that folder only, not its subfolders), a playlist, or a search result (kept as it was when you played it). Choosing the song that is already playing, in any list, only resumes it and opens Now Playing: its queue stays.
- The playback mode (Normal, Repeat All, Repeat One, Shuffle) is set in Settings.
- NORMAL: plays to the end of the list and stops on the last song, paused at 0:00. A then starts again from the first song. Left on the first song goes to the last one.
- REPEAT ALL: wraps around. REPEAT ONE: repeats the song; Left/Right still change songs.
- SHUFFLE: plays the chosen song first, then every other song of the list exactly once in random order, then stops. A starts a new shuffle pass.
- Play Next puts a song right after the current one (the newest Play Next goes first), also in SHUFFLE. The Play Next songs are kept across an ordinary restart, but either Secret Settings deletion discards them. Missing files are dropped. If the last song came from Play Next, it resumes with the rest of that list, then the list you started from continues.

## Song Options

- Play, Play Next, Add to Playlist, Song Info. Opened from a playlist: also Remove from Playlist.
- For the song that is playing: Play / Pause replaces Play, and Play Next is hidden. Remove from Playlist appears only when opened from a playlist; each action retains its meaning after a reorder.
- Song Info: Title, Artist, Album, Genre, Filename, Format (for MP3 with the bitrate, e.g. MP3, 320 kbps, or MP3, VBR), Sample Rate, Stereo/Mono.
- Add to Playlist lists your playlists and + New Playlist, which creates a playlist and adds the song in one step. Opened from a song in a playlist, that playlist is left out.
- Rename (Secret Settings > Rename files On): type a new file name; the extension (.mp3 / .gac) stays. Delete (Delete files On): asks for confirmation, No first. See File operations below.
- Convert to GAC / Convert to MP3 (Secret Settings > Conversion MP3>GAC / GAC>MP3), after Song Info, for a song of that type. See Conversion below.

## Album, artist, genre and folder options

- START on an album, artist or genre (in Albums, Artists, Genres, or an artist's albums) or on a folder in Folders opens its options: Play, Shuffle Play, Play Next, Add to Playlist, Info.
- Play: plays all its songs from the first (a folder: only the songs directly in it, not its subfolders). Shuffle Play: sets the playback mode to Shuffle and starts with a random song.
- Play Next: puts all its songs (up to 32 Play Next songs in total) after the current song, in their order. With nothing playing it plays them.
- Add to Playlist: adds all its songs to a playlist or a new one; a progress line shows while they are added, B stops it.
- Info: name, number of songs, total length, and the number of albums (artist, genre) or the folder path.
- Rename (Rename files On): folders rename the folder; albums, artists and genres write the new name into the tags of all their songs (see File operations). Delete (Delete files On) deletes all their song files.
- Convert to GAC / MP3 (Conversion On), after Info: converts all their songs of that type; a folder includes its subfolders. See Conversion below.

## Playlists

- The playlist list ends with + New Playlist, below a thin grey line. Names must not be empty or repeat an existing name (ignoring case and accents); up to 63 bytes.
- A opens a playlist; A on a song plays the playlist from that song.
- Every playlist ends with + Add Songs, below a thin grey line. A on it opens Search (every song while nothing is typed; the header says Add Songs): START + A adds the selected song to the playlist ("Added") and Search stays open for more; B with nothing typed, or START + B, goes back to the playlist, which shows the new songs at its end.
- Hold A and press Up / Down to move the selected song or playlist (songs and playlists never pass + Add Songs or + New Playlist).
- START on a song in a playlist: Song Options, including Remove from Playlist.
- START on the playlist list: Playlist Options with Play, Shuffle Play, Play Next, Info, Convert to GAC / MP3 (Conversion On), Rename and Delete Playlist. Rename and Delete Playlist do not depend on the Secret Settings switches. Delete asks for confirmation; No is selected first.
- There is no limit on the number of playlists or songs. A song whose file is missing is not shown (a moment after the list appears: the files are checked while it is shown) and skipped during playback; Update Library removes it from the playlist (only a file that is really gone: never because the card could not be read).

## Search

- Type with the GBAWriter controls below. Results update while you type, ignoring case and accents and leading spaces. Song Title finds any word start ("clack" finds "Click Clack", each song once; words end at spaces and punctuation, not apostrophes; first 32 words); Filename, Album and Artist find the start of the name.
- The field searched is set in Settings. Word search needs one Update Library with v1.2; until then Song Title matches title starts.
- START + Up / Down: move in the results. START + L / R: page up / down. START + Left / Right: move the text cursor.
- START alone (pressed and released with no other button): Song Options for the selected result. START + SELECT does the same.
- START + A: play the selected result (the results become the queue). START + B: back to Home.
- Releasing START after START + another button does nothing more, also when the screen has changed (for example to Now Playing after START + A).
- B alone deletes a character. Search keeps its text and position when you come back from Song Options or Now Playing.
- Opened from + Add Songs in a playlist (header Add Songs): START + A adds the selected song to that playlist instead of playing it; B with nothing typed or START + B goes back to the playlist.

## Typing (GBAWriter input)

- Hold a direction, then press B / A / R for its first / second / third letter: Up a b c, Right h i j, Down n o p, Left t u w. Hold L as well for the second layer: Up d e f, Right k l m, Down q r s, Left x y z.
- Keep Right held and press R twice for g; keep Left held and press R twice for v.
- A alone: space. B alone: delete. A short R press arms Shift for one letter; holding R alone turns Caps on; another R press turns it off.
- SELECT alone types a period; keep SELECT held and press Up/Down for digits, R/L for , ' " : ! ?, Right/Left for ( ) / ; @ # % & _ + = -.
- Accents: hold SELECT, then type the letter; or keep the letter's buttons held and press SELECT. Press the letter button again to cycle the accents.
- Top right: the held direction's letter group and Shift/Caps, each in a fixed place.
- Naming a playlist: START + A saves, START + B cancels.

## Settings

- SELECT opens Settings from any screen except Search and naming a playlist. B returns to the screen you came from.
- Playback Mode (first row): Normal, Repeat All, Repeat One, Shuffle; A changes it.
- Update Library: scans every folder under /gbamp3 and rebuilds the library on the GBA. Manual only; playback stops while it runs. Unchanged files reuse their stored tags and covers. Modeled SD card: about 60 ms per song plus 0.5-5 s per album cover; libraries older than v2.1 are not used.
- Convert all (only while Secret Settings > Conversion is on and the library has a song to convert): converts every song of that type. See Conversion below.
- Output: Stereo (default) or Mono, for MP3 and GAC alike.
- Volume Boost: Off (default) or On. On starts at 0 dB; set the level with Up / Down in Now Playing. Turning it off returns to 0 dB. Loud peaks are softened by a limiter instead of clipping; quiet recordings gain the most.
- Search Field: Song Title, Filename, Album or Artist.
- Show songs as: Titles (default) or File Names; A changes it. Applies to every song list (Songs, a genre's, artist's or album's songs, Search results, playlists); the Songs list is sorted by file name with File Names.
- About: eight pages, Left / Right to move (no wrap: Right on the last page and Left on the first do nothing): version and contact, a control summary, the license, the parts made by others (MP3 decoder, SD card and files, text and fonts) with their licenses, notes, and Secret Settings. On page 8 press A to open Secret Settings; the music stops.

## Secret Settings and deletion

- Opening Secret Settings (About page 8, A) stops the music; press A in Now Playing to continue afterwards.
- Reorder menus (A+DPAD): Off / On. A toggles it; default Off. The four independent orders are stored with configuration. Playlist/song swapping does not depend on this switch.
- Auto-update library: Off / On. A toggles it; default Off. When On, Update Library runs by itself at every start (the screen shows Updating library..., then Home). Stored with configuration.
- Show Songs: MP3 + GAC (default), only MP3 or only GAC; A changes it. Folders and the library (Songs, Artists, Albums, Genres, Search) then list only those files. When you leave Secret Settings with B after changing it, Update Library runs at once. Playlists keep all their songs.
- Letter Box: Off (default) / On / Only L+DPAD; A changes it. Only L+DPAD shows the box only for L + Up / Down.
- Rename files: Off (default) / On. On adds Rename to Song Options and to album, artist, genre and folder options.
- Delete files: Off (default) / On. On adds Delete to Song Options and to album, artist, genre and folder options.
- Conversion: Off (default) / MP3>GAC / GAC>MP3; A changes it. MP3>GAC adds Convert to GAC, GAC>MP3 adds Convert to MP3, wherever there is a song to convert: Song Options, album, artist, genre, folder and playlist options, and Settings > Convert all.
- Hold START to unlock: On (default) / Off. On: START unlocks only when held 1 s (the screen stays black until then); a shorter press does nothing (a sleeping GBA sleeps again). Off: one START press unlocks.
- Delete configuration: first A replaces that row with Sure?; a second A executes. B, moving away, or A+B cancels the question and stays in Secret Settings. This removes only /gbamp3/GBAMP3.SYS/STATE0.DAT, STATE1.DAT, QUEUE0.DAT and QUEUE1.DAT. The current song and Play Next are discarded; settings and menu orders reset to defaults. Playlist and music files remain.
- Delete library index: the same confirmation and cancellation rules. This removes only /gbamp3/GBAMP3.SYS/LIB0.IDX, LIB1.IDX, QUEUE0.DAT and QUEUE1.DAT. The current song and Play Next are discarded; unrelated settings and menu orders are saved. After restarting, use Update Library to restore library browsing. Folder browsing and playlists remain available.
- On success, the app says `Deletion succeeded. Restart system now.` No button works until you physically restart. A failure or partial removal instead says `Deletion failed. Restart system now.` and also locks the app; it never claims success. Missing files count as already absent. On a partial failure an older surviving slot may load after restart: inspect the card before retrying. The GBAMP3.SYS folder, music, GAC, playlist and unlisted files are never deletion targets.

## Conversion (Secret Settings > Conversion)

- Convert asks first: the number of songs, about how long it takes, and "Replaces the originals. OK?"; No is selected.
- Yes stops the music and converts the songs one after another. The screen shows the song (n of m), its progress and the time left. B cancels: the song being converted is dropped, the songs done stay converted. L+R turns the screen off, START turns it on again; the conversion goes on meanwhile.
- Each new file is written under a temporary name and read back and checked; only then does it take the song's name (song.gac for song.mp3 and the other way round), and the original is deleted. Playlists and Play Next follow to the new file. If a file of the new name exists already, the song is skipped and nothing is overwritten. Afterwards Update Library runs; the message says how many songs were converted (and failed or were skipped).
- To continue after cancelling, convert again: the songs done are of the other type now.
- MP3 > GAC: a GAC v4 cache at 32768 Hz (stereo layout) with the song's tags and cover, about 9 times as long as the song (a 4-minute song: about 36 minutes).
- GAC > MP3: an MP3 at 32000 Hz, 128 kbps joint stereo, with an ID3v2.4 tag (title, artist, album, album artist, genre, track, disc) and the cover, about 10 times as long as the song (a 4-minute song: about 41 minutes). Encoder: shine (LGPL 2.0).

## File operations (Rename / Delete files On)

- Renaming or deleting stops the music and forgets the current song (Play Next stays), then runs Update Library.
- Rename: names must not be empty, repeat a name in the same folder, or contain \ / : * ? " < > |. A renamed song or folder keeps its place in every playlist and in Play Next.
- Rename an album, artist or genre: the new name (up to 95 bytes, genre 63) is written into the album, artist or genre tag of every song in it: the ID3v2 tag of MP3s (other tag fields, pictures and the audio stay as they are; a song without an ID3v2 tag gets one) and the metadata of GAC files. Renaming Unknown Album, Artist or Genre names the songs without that tag. When the new name does not fit in a tag's free space the song file is written again (as NAME.tmp, replacing the original only when complete). A few rare tag kinds are left unchanged; the message then says how many songs were renamed.
- Delete removes the song files from the card; there is no undo. A deleted song is removed from its playlists by the Update Library that follows. Folder Delete removes the songs directly in the folder and then the folder itself if nothing else is left in it (subfolders and other files are kept).

## What is remembered

- Saved on the card: the last song, the playback mode, Stereo/Mono, the search field, Volume Boost (on/off and level), the four menu orders and enable switch, the Secret Settings switches, the Play Next songs, and playlists. At startup the last song is shown paused at 0:00 unless configuration or library index was deleted.
- Not saved: the playing position and whether music was playing.
- Changes (Settings included) are written about two seconds later, while paused or when the audio buffer is full. A write-protected card still plays; changes are simply not kept.

## Audio

- MP3 plays at 32768 Hz in Stereo and Mono, signed 8-bit: one sample per cycle of the GBA's 32.768 kHz output, like 32768 Hz GAC. At the lower rates of v0.9.9-v1.0 MP3 could turn screechy after a seek, pause or song start on real hardware. Stereo keeps 4 of the 32 frequency bands (up to about 2.8 kHz at 44.1 kHz; 8 of 32 at 16-24 kHz, the same range). In Mono, MP3s at 32-48 kHz keep 8 of the 32 frequency bands instead of 6 (7 at 48 kHz): up to about 5.5 kHz at 44.1 kHz (before: 4.1 kHz), 5.2 kHz at 48 kHz, 4 kHz at 32 kHz. Constant-bitrate files above 256 kbps keep 6 bands so the heaviest files stay within the CPU. GAC plays at its cache rate (32768 or 44150.6 Hz) with noise-shaped 8-bit output; Mono plays the stored mid channel.
- An unreadable or invalid song shows an error on Now Playing; other songs remain playable.
- About 0.3 s (GAC 44 kHz) to 0.8 s (MP3) of audio is buffered and topped up before playlist writes. If a slow card empties it, output pauses briefly and resumes.

## Qualification limits

Host tests run the production library, folder, queue, playlist, settings and text code on real FAT images. Exact-ROM tests require an external mGBA build and model the SD driver, decoder and audio interrupt with emulated button presses. Neither is physical Supercard, headphone-listening or electrical card validation. See the Build the ROM section of the README for the commands; this is not a guarantee for arbitrary files or cards.

## About / licence

gbamp3 11.0. Copyright (C) 2026 Halim Jarrar. halimj.itch.io / gba@halim-jarrar.de.

gbamp3 is free software, licensed under the GNU General Public License version 3 or later (GPL-3.0-or-later), with an additional permission for linking with the Helix MP3 decoder (the text is in NOTICES.md). The Helix MP3 decoder is licensed under the RealNetworks Public Source License 1.0. No warranty: gbamp3 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY.

Source code: github.com/Xinon232/gbamp3

Credits:

- Helix DNA Client technology included. Copyright (c) RealNetworks, Inc., 1995-2002. All rights reserved. Helix MP3 decoder by Jon Recker and Ken Cooke, via Phil Schatzmann's arduino-libhelix: RPSL 1.0.
- SuperFW SD card driver, text renderer and fonts (UNSCII, GNU Unifont): David Guillen Fandos, GPL-3.0-or-later.
- EZ-Flash Omega and EverDrive GBA SD drivers (gbaSD): Halim Jarrar, GPL-3.0-or-later; the EverDrive code after edio-gba by krikzz (MIT), the Omega driver after the EZ-Flash Omega kernel (Apache-2.0).
- FatFs: ChaN, BSD-style licence.
- GBAWriter input engine and GBAVocab accent folding: Halim Jarrar, GPL-3.0-or-later.
- shine MP3 encoder: Gabriel Bouvigne, Pete Everett, Romain Beauxis and contributors, LGPL-2.0.
- Built with devkitPro / devkitARM.
