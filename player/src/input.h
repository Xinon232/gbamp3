/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). 60 Hz button sampler for the v0.8.2 control model. */
#pragma once
#include <stdint.h>
enum {KEY_A=1,KEY_B=2,KEY_SELECT=4,KEY_START=8,KEY_RIGHT=16,KEY_LEFT=32,KEY_UP=64,KEY_DOWN=128,KEY_R=256,KEY_L=512};
#define INPUT_HOLD 30         /* frames: LEFT/RIGHT hold -> seek (0.5 s) */
#define INPUT_REPEAT 18       /* frames: seek repeat while held (0.3 s) */
#define INPUT_LIST_DELAY 24   /* frames: UP/DOWN auto-repeat start */
#define INPUT_LIST_REPEAT 5
/* v1.6 long-list hold schedule (frames at 60 Hz), one speed for every
 * format: what the heaviest MP3 (48 kHz 320 kbps, slow card) keeps up with
 * while the screen shows every step whole. Up/Down: a row every 7 frames
 * (8.6 rows/s) from 0.4 s. Left/Right: a page every 24 frames (2.5 pages/s)
 * from 0.4 s. L + Up/Down: a letter every 24 frames from 0.4 s. */
#define INPUT_ROW_REPEAT 8
#define INPUT_PAGE_REPEAT 30
#define INPUT_LETTER_REPEAT 30
struct InputFrame {
 uint16_t held;      /* buttons currently down */
 uint16_t pressed;   /* went down this frame */
 uint16_t tap;       /* released this frame without being consumed (short press) */
 uint16_t repeat;    /* UP/DOWN press or auto-repeat; LEFT/RIGHT hold-repeat */
 uint8_t lr_chord;   /* L and R became held together this frame (lock) */
 uint8_t a_chord;    /* A is held and UP/DOWN was pressed/repeated (reorder) */
 uint16_t hold_frames[10];
 /* v1.5 long lists: steps this frame (a press counts one, then the hold
  * schedule below; several when a UI frame spans several video frames).
  * rows[0]/rows[1]: Up/Down without L (8.6 rows per second);
  * letters[0]/[1]: Up/Down while L is held; pages[0]/[1]: Left/Right. */
 uint8_t rows[2],letters[2],pages[2];
 uint16_t elapsed;   /* video frames since the previous UI frame */
};
void input_reset(uint16_t held_now);
/* Record keys seen down between frames (called from long-running work). */
void input_poll(uint16_t held);
/* Sample once per UI frame; `elapsed` video frames since the last call.
 * Keys consumed by chords/long presses never tap. */
void input_frame(uint16_t held, unsigned elapsed, struct InputFrame *out);
/* Suppress the release tap of currently held keys (e.g. after a screen change). */
void input_consume(uint16_t keys);
