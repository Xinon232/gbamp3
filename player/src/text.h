/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder) (glyph lookup: SuperFW, GPL-3.0-or-later).
 * UTF-8 text: SuperFW 16 px font for primary UI, 5x7 font for small labels,
 * GBAVocab Latin accent folding for sorting and search. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define TEXT_HEIGHT 16
#define SMALL_HEIGHT 7
#define SMALL_DESCENT 2   /* g j p q y reach two rows below the baseline */
#define SMALL_ADVANCE 6
/* Next Unicode scalar; malformed bytes decode as U+FFFD and advance one byte. */
uint32_t text_next(const char **s);
/* Pixel width of a string in the primary font. */
unsigned text_width(const char *s);
/* Draw into the framebuffer (v1.7: mode 4 through gfx.c when active): string starts at screen x, its first
 * `skip` pixel columns are omitted, drawing is clipped to [x0, x1). Only set
 * pixels are written; callers clear the background. */
void text_draw(int x, int y, int x0, int x1, const char *s, unsigned skip, uint16_t color);
/* Incremental column reader for scrolling text (marquee). */
struct TextCursor {const char *s;const uint16_t *d[3];unsigned a,i,rep,w,n;};
void text_cursor(struct TextCursor *c, const char *s, unsigned skip);
uint16_t text_cursor_next(struct TextCursor *c);   /* 16 row bits, 0 past the end */
/* Same, but paints the background of the whole [x0,x1) x 16 box in one pass. */
void text_draw_bg(int x, int y, int x0, int x1, const char *s, unsigned skip, uint16_t fg, uint16_t bg);
/* Truncate-with-ellipsis helper: bytes of s that fit in `width` px when a
 * trailing "..." is appended (returns strlen when the whole string fits). */
size_t text_fit(const char *s, unsigned width, unsigned *fits_whole);
/* 5x7 label font (upper and lower case); accented Latin letters show their base letter. */
unsigned small_width(const char *s);
void small_draw(int x, int y, const char *s, uint16_t color);
/* Folding: case- and Latin-accent-insensitive scalar stream (GBAVocab rules). */
struct Fold {const char *p; int latin;};
void fold_init(struct Fold *f, const char *s);
uint32_t fold_next(struct Fold *f);
/* <0, 0, >0 like strcmp on folded text; prefix!=0 compares only query length. */
int fold_compare(const char *a, const char *b, int prefix);
/* Folded key as UTF-8 bytes (ASCII lowercase for mapped Latin), NUL padded
 * to exactly `cap` bytes; unsigned byte order equals fold_compare order for
 * keys that fit. Returns bytes used. */
size_t fold_key(char *dst, size_t cap, const char *src);
uint32_t fold_point(struct Fold *f, uint32_t c);   /* 12.0: one code point; 0: dropped */
/* Append one scalar as UTF-8; returns bytes written (0 if it does not fit). */
size_t utf8_put(char *dst, size_t cap, uint32_t code);
