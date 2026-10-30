/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v1.7 video; v2.1: every screen, Now Playing too,
 * uses mode 4 (8-bit palette pixels, two pages): all drawing goes to the
 * hidden page.
 * gfx_commit() shows it at the next vertical blank (switched from the sound
 * interrupt, or here while no sound plays), after which the changed lines are
 * copied to the other page so both stay equal. A picture is therefore always
 * shown whole: never half drawn, never half moved. */
#pragma once
#include <stdint.h>
extern unsigned gfx_m4;                  /* 1 (v2.1: always mode 4) */
/* Palette: 0 the black backdrop, 1 .. GFX_UI_COLORS-1 the interface colours
 * (added on first use; 7 are used), then COVER_COLORS for the cover. */
#define GFX_UI_COLORS 16
extern volatile unsigned gfx_flip_state; /* 0 idle, 1 switch pending, 2 switched (copy pending) */
void gfx_init(void);
/* Colour (15-bit) as its palette index (entries are added on first use; 0
 * is the black backdrop). */
uint16_t gfx_pixel(uint16_t rgb);
/* Line y of the drawing page as halfwords: 240 bytes (two pixels per
 * halfword; VRAM takes no byte writes). */
volatile uint16_t *gfx_line(int y);
void gfx_fill(int x, int y, int w, int h, uint16_t rgb);
void gfx_plot(int x, int y, uint16_t rgb);
/* Lines [y0, y1) of the hidden page changed (the drawing functions mark
 * their own lines). */
void gfx_mark(int y0, int y1);
/* Hidden-page lines [dy, dy+n) from shown-page lines [sy, sy+n). */
void gfx_copy_lines(int dy, int sy, int n);
/* Wait until the hidden page may be drawn on (a pending switch done, the
 * other page brought up to date). Call before drawing. */
void gfx_ready(void);
/* Show what was drawn. Without sound the switch is done here, at
 * the next vertical blank. */
void gfx_commit(void);
/* Like gfx_commit but copies the changed lines to the shown page at once,
 * without waiting (progress text while Update Library runs). */
void gfx_commit_now(void);
/* Nonzero from the start until the first picture is shown (the display is
 * blanked: white). */
unsigned gfx_blanked(void);
/* Screen off (lock: backdrop black) or back on. */
void gfx_screen(unsigned on);
/* Sound interrupt: switch pages if one is pending and the display is in its
 * vertical blank. */
__attribute__((long_call)) void gfx_flip_irq(void);
/* v2.1: a cover record (cover.h) on the hidden page at (x, y), x even. */
void gfx_cover(int x, int y, const volatile uint16_t *rec);
