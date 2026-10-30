/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). IWRAM decoder overlay (see overlay.c). */
#pragma once
enum {DECODER_NONE=0,DECODER_MP3=1,DECODER_GAC=2,DECODER_JPEG=3,DECODER_CONV=4,DECODER_MENC=5};   /* MENC (v2.0): resampler + MP3 encoder (GAC -> MP3) */   /* JPEG: covers during Update Library (v0.9.8); CONV: resampler + GAC encoder (conversion) */
/* Load the MP3 or GAC decoder into the shared IWRAM region unless it is
 * already there. stats.decoder names the loaded overlay. */
void decoder_use(unsigned which);
/* The same without clearing the GAC decoder state (GAC -> MP3 conversion). */
void decoder_use_keep(unsigned which);
/* The SD driver code for the cart found at boot: the SuperCard read code
 * (ezfo 0) or the EZ-Flash Omega driver (ezfo 1), see player.ld. */
void cart_use(unsigned ezfo);
