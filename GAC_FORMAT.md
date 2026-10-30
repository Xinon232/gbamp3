# GAC version 4 — binary format

Made by Halim Jarrar, (C) 2026. Format, converter and decoder code:
GPL-3.0-or-later with the additional permission in NOTICES.md.

`.gac` means GBA audio cache, **not MP3**. Version 4 (gbamp3 v0.9.0) replaces
v3's fixed 2-tap ADPCM with per-block adaptive LPC and entropy-coded
residuals: files are about 30% smaller at equal or better measured quality
(decision record below). There is **no backward compatibility**: v0.9.0
plays v4 only, and GAC v1–v3 files show **OLD GAC - RECONVERT** (reconvert
them from the MP3). Older ROMs cannot play v4. `tools/gac4.py` is the
reference encoder/decoder; `player/src/gac4.c` (with `gac4_arm.S`) is the GBA
parser/decoder, and `tests/test_gac4.py` requires them to agree bit for bit.

## File layout

|Offset|Bytes|Contents|
|---|---:|---|
|0|64|Header|
|64|512|Metadata (GACMETA3)|
|576|0, 8192 or 2048|Optional 64x64 art (32x32 before gbamp3 v1.8)|
|seek|4 × blocks|Seek table|
|…|0..511|Zero padding to a 512-byte boundary|
|audio|1024 × blocks|Blocks|

All integers are little-endian, unsigned unless stated. The file length must
be exactly `audio + blocks*1024`, at most 0x7fffffff. Blocks start on
512-byte file offsets, so each block read covers exactly two SD sectors.

## Header (64 bytes)

| Offset | Bytes | Meaning |
|---|---:|---|
|0|8|ASCII `GBCACHE` followed by NUL|
|8|2|Version = 4 (1, 2 or 3: an old version, reported as such)|
|10|2|Header length = 64|
|12|4|Codec = 3, adaptive LPC + Huffman|
|16|4|Samples/second/channel: 32768 or nominal 44151 (nothing else plays)|
|20|4|Layout: 2 = mid + half-rate side (stereo), 1 = mid only (mono)|
|24|4|Block bytes = 1024|
|28|4|Blocks, 1..total|
|32|4|Total frames, 1..0x7fffffff; at most 8192 per block|
|36|4|Seek table offset: 576 + art length (576, 8768 or 2624)|
|40|4|Audio offset = seek offset + 4 × blocks, rounded up to a multiple of 512|
|44|4|Metadata length = 512|
|48|4|Art length: 0, 8192 (64x64) or 2048 (32x32)|
|52|4|Metadata FNV-1a32|
|56|4|Art FNV-1a32, zero without art|
|60|4|Seek table FNV-1a32|

GBA timer periods 512/380 at core 16777216 give 32768/44150.568421 samples
per second; the 44151 rate plays about 10 ppm slow. 44151 Hz uses the
65.536 kHz PWM output setting (32.768 kHz PWM would alias it). The 65536 Hz
rate of earlier versions is gone.

FNV-1a32 starts at 2166136261, XORs each byte then multiplies by 16777619
modulo 2^32.

## Metadata and optional artwork

**GACMETA3** (unchanged from v3): magic `GACMETA3` (8 bytes); uint16 track
number, track total, disc number, disc total (bytes 8..15; 0 = unknown); then
five NUL-terminated UTF-8 fields, zero padded: title (bytes 16..143), artist
(144..239), album (240..335), album artist (336..431), genre (432..495).
Bytes 496..511 are zero. Each field holds at most 127/95/95/95/63 bytes, cut
on a character boundary, and contains no control characters (C0, DEL, C1);
overlong or surrogate UTF-8 is invalid. GACMETA2 is no longer accepted.
Metadata checksum or format failure rejects playback; the library then lists
the file by name.

Optional art at byte 576: 64 rows of 64 RGB555 pixels (bit 15 ignored on
display). The converter writes 64x64 art from gbamp3 v1.8; 32x32 art (2048
bytes, written before) still plays and is shown doubled, so existing files
need no reconversion. An art checksum failure hides the cover but still
plays the audio. From gbamp3 v2.1 the player reads the art at Update
Library, reduces it to at most 240 colours and keeps it in its library as the
cover of the song's album; the file format is unchanged.

## Seek table

One u32 per block: the first frame of that block. Entry 0 is 0, entries
increase strictly, consecutive entries (and the last entry and the total) are
at most 8192 apart. The player checks the whole table and its checksum when
it opens the file; a bad table rejects the file. Seeking finds the last
block that starts at or before the target frame by binary search and decodes
from there, discarding the frames before the target.

## Blocks (1024 bytes, independently decodable)

|Offset|Bytes|Meaning|
|---|---:|---|
|0|2|Frames in this block, 1..8192 (the player never plays past the total)|
|2|1|Mid LPC order (low nibble), side LPC order (high nibble); values above 8 mean 8|
|3|1|Bit 0: side silent (no side stream in this block, side = 0)|
|4|1|Mid initial step index (clamped to 1..63)|
|5|1|Side initial step index (clamped to 1..63)|
|6|om + os|Reflection coefficient codes: mid, then side|
|…|0 or 1|Zero byte if om + os is odd|
|…|2 × om|Mid history, signed 16-bit, most recent first|
|…|2 × os|Side history, signed 16-bit, most recent first|
|…|rest|Bitstream|

In the mono layout, and when the side is silent, `os` counts as 0 and there
is no side stream.

### Predictor

Reflection code j (a signed byte) means k = sin(j/127 · π/2) in Q15
(`REFLECT` in `gac4.py`: `round(32767·sin(...))`, j = −128 gives −32767). The
step-up recursion turns k_1..k_n into Q12 predictor coefficients with integer
arithmetic only:

    for m = 0..n-1:  a'[i] = clamp16(a[i] - ((k_m * a[m-1-i] + 16384) >> 15)),  i < m
                     a'[m] = (k_m + 4) >> 3

Each sample, with history h[0] = previous sample:

    pred = (2048 + sum_k a[k] * h[k]) mod 2^32 (as signed), then >> 12
    x    = clamp16(pred + q * STEP[step index])

`>>` is an arithmetic shift. `STEP[i] = round(2^(2 + (i-1)/4))` for
i = 1..63 (about 1.5 dB apart).

### Bitstream

Bits are read least significant first from little-endian 16-bit words; bits
past the end of the block read as zero. Every code is at most 10 bits long
and every 10-bit pattern starts a code, so a decoder reads each code with one
lookup in a 1024-entry table and **any byte pattern decodes**. The tables are
canonical Huffman codes computed (package-merge, length limit 10) from the
probability models in `gac4.py`; `player/src/gac4_tables.h` is generated from
them and checked by the tests.

The stream is a sequence of **units** of 64 frames: mid subblock (frames
0..31), mid subblock (32..63), then, in the stereo layout unless the side is
silent, one side subblock (32 half-rate side samples). A block holds
ceil(frames/64) units; frames past the block's count are decoded and
discarded.

A **subblock** starts with one header code:

- step change d (−6..6) and residual table t (0..3): step index = clamp(previous + d, 1, 63);
- zero subblock: all 32 residuals are 0, no residual codes follow;
- escape with table t: a raw 6-bit absolute step index follows (0 means 1).

The previous step index starts at the block's initial index for that stream
and changes only with coded subblocks. Then follow 32 residuals q:

- Tables 0 and 1 code **pairs**: each code gives two residuals in −7..7, or
  an escape followed by two raw signed 12-bit residuals.
- Tables 2 and 3 code single residuals in −24..24, or an escape followed by
  one raw signed 12-bit residual.

Residual models: p(0) = z, p(±k) = (1−z)/2 · (1−θ) · θ^(k−1), with
(z, θ) = (0.70, 0.25), (0.50, 0.38), (0.30, 0.55), (0.13, 0.75) for tables
0..3; pair probabilities are products. Header model: p(d) = 0.38 for d = 0,
else 0.105 · 0.6^(|d|−1), times table probabilities (0.40, 0.36, 0.20, 0.04);
zero subblock 0.07; escape 0.004 · table probability.

### Stereo reconstruction

Side sample k of a unit represents frame 2k+1. With `prev` the previous side
sample (the block's newest side history sample at block start, 0 if the side
is silent or has order 0):

    side(2k)   = (prev + S[k]) >> 1
    side(2k+1) = S[k]
    L = M + side,  R = M - side      (not clamped here)

The encoder stores M = (L+R)>>1 and S = (L−R)>>1, the side lowpassed (31-tap
windowed sinc, cutoff 0.45 of the side Nyquist) and decimated at odd frames.

## Player output

Unchanged from v3: each output channel is quantized to signed 8-bit with
first-order error feedback, starting from zero error at every stream start
(track start, seek, sound-mode change):

    w = clamp(v + e, -32768, 32639);  q = (w + 128) >> 8;  e = w - q*256

Mono output of a stereo file plays M only; the side subblocks are parsed but
not synthesized. Mono-layout files play M on both outputs in either mode.
`gac4.quantize8` is the reference.

## Encoder (tools/gac4.py)

FFmpeg decodes and resamples to signed 16-bit stereo at the target rate. The
file is cut into segments of 1536 units that are coded in parallel. Within a
segment, units go into the current block until the next one would not fit;
that unit then starts the next block (about 97% of each block is used). A
block's LPC order (1, 2, 4, 6 or 8) and coefficients come from a 2048-frame
look-ahead window from the block start, and its histories are the source
signal's own samples, so blocks never depend on each other.

Per subblock the encoder tries seven step indices around an estimate from the
open-loop residual (never below index 14 in stereo, 20 in mono), quantizes
closed-loop with a small rounding dead zone, picks the cheapest residual table
and keeps the step with the lowest `squared error + λ · bits` (λ = 9000
stereo, 15000 mono; side errors count twice, as a side sample spans two
frames and enters both channels). There is no user quality setting. Encoding
runs at about 4–5× realtime (v3: about 20×).

No checksum covers the audio payload: corrupted payload decodes as different
audio. This is not an integrity or authentication format.

## Decision record (v0.9.0 plan, phase 2)

Corpus: 21 items, up to 60 s each (668 s in total): 14 freely licensed music
recordings (solo and ragtime piano, string orchestra, pop with vocals, rock,
jazz, hip-hop drums and bass, orchestral electronic, wind ensemble, two
trumpet recordings, whale song), three read-speech clips, a bird call and the
two synthetic v0.8.0 stress fixtures. Each item was decoded to a 16-bit
reference at the target rate; v3 (production `gac.c`) and v4 outputs went
through the 8-bit noise-shaped output stage and were compared with the
unquantized reference: SNR, segmental SNR (1024-frame segments above about
−50 dBFS, clamped to −10..60 dB) and A-weighted SNR. Averages over the 21
items, v4 minus v3:

|Rate, layout|v4 size vs v3|bits/frame v4 / v3|SNR|seg. SNR|A-weighted SNR|
|---|---:|---:|---:|---:|---:|
|44151, stereo|69.8%|3.73 / 5.34|+0.17 dB|+0.05 dB|+0.11 dB|
|32768, stereo|70.0%|3.74 / 5.34|+0.13 dB|−0.00 dB|+0.09 dB|
|44151, mono|73.0%|2.60 / 3.56|+0.45 dB|+0.12 dB|+0.15 dB|
|32768, mono|73.5%|2.62 / 3.56|+0.39 dB|+0.07 dB|+0.18 dB|

Per item the result varies with the content: sparse and quiet material
(speech, hip-hop, jazz, whale song) shrinks to 30–60% of v3; dense material
(pop, rock, strings, the stress fixtures) stays near v3's size with better
quality. The largest per-item loss is the whale song (−1.5 to −1.8 dB
A-weighted, at under half of v3's size). What was tried and kept or dropped:

- LPC order 8 per block: kept. Order 16 would save only about 0.1 bit/sample.
- Huffman instead of Rice codes: kept (within 0.1 bit of the static entropy);
  pair tables for small residuals: kept (single codes cannot go below 1 bit).
- Rate-distortion step choice: kept; the rule-based step alone reached only
  about 87% of v3's size.
- Dead-zone rounding (0.2): kept, about −7% size at equal quality.
- Delta-coded subblock headers: kept, about −3%.
- 8-bit reflection coefficients instead of 16-bit direct form: kept.
- Noise-feedback shaping of the coding noise: dropped, it lowered all three
  metrics at equal size.
- Letting quiet passages use coarser steps than index 14: dropped, it cost
  segmental and A-weighted SNR.
- Quarter-rate side: not implemented; the rate-distortion search already
  gives the side few bits where it is weak.

GBA cost (exact ROM, emulator, 44151 Hz stereo): decode plus SD read 64% of
the CPU on dense real music (180k cycles per video frame; budget 70%), 0
underruns in 60 s with and without 50 modeled SD busy polls. See
V090_VERIFICATION.md. Listening on real headphones and hardware tests are
still to be done.
