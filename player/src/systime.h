/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Included by Helix mp3dec.c only under PROFILE, i.e.
 * test builds (build.py with PROFILE_MP3=1):
 * Helix's PROFILE hooks in mp3dec.c feed cycle counters instead of printf.
 * mp3_prof[]: 0 side info, 1 reservoir copy, 2 scale factors, 3 Huffman,
 * 4 dequantize + stereo, 5 IMDCT (+ antialias, overlap), 6 subband
 * (FDCT + polyphase). Read from the final EWRAM dump by symbol. */
#pragma once
unsigned mp3_prof[8];
static inline long systime_get(void){
 unsigned h,l,h2;do{h=*(volatile unsigned short*)0x400010c;l=*(volatile unsigned short*)0x4000108;h2=*(volatile unsigned short*)0x400010c;}while(h!=h2);
 return (long)(h<<16|l);
}
static inline void mp3_prof_add(const char *f,long t){
 unsigned k=f[0]=='d'?1:f[0]=='H'?3:f[0]=='D'?4:f[0]=='I'?5:f[0]=='S'?6:f[6]=='S'&&f[7]=='i'?0:2;
 mp3_prof[k]+=(unsigned)t;
}
#define printf(f,t) mp3_prof_add(f,t)
