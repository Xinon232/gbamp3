/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). C bridge to GBAWriter's unchanged input engine
 * (writer_core.cpp, GPL-3.0) for one-line text fields: search and names. */
#pragma once
#include <stdint.h>
enum {WRITER_UP=1,WRITER_DOWN=2,WRITER_PAGE_PREV=4,WRITER_PAGE_NEXT=8,
      WRITER_START_A=16,WRITER_START_B=32,WRITER_START_SELECT=64,WRITER_TEXT=128,WRITER_FULL=256,
      WRITER_START_ALONE=512};   /* START pressed and released with no other button */
void writer_begin(const char *initial);
/* One frame with GBA KEYINPUT-style held bits (A=1 ... L=512). Returns WRITER_* flags. */
unsigned writer_frame(uint16_t gba_held);
const char *writer_text(void);
unsigned writer_caret(void);
int writer_shift(void);
int writer_caps(void);
const char *writer_group(void);
