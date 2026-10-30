/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Text column blitter: ARM code in IWRAM (linked via
 * the .iwram.o rule), since per-pixel loops run ~10x slower from cartridge ROM
 * at the SuperCard-safe wait states. Column k, bit j -> dst[k + 240*j]. */
#include <stdint.h>
void blit_cols(volatile uint16_t *dst,const uint16_t *cols,unsigned n,unsigned fg,unsigned bg,unsigned opaque){
 for(unsigned k=0;k<n;k++){
  unsigned v=cols[k];volatile uint16_t *d=dst+k;
  if(opaque){for(unsigned j=0;j<16;j++,v>>=1,d+=240)*d=(uint16_t)((v&1)?fg:bg);}
  else{for(;v;v>>=1,d+=240)if(v&1)*d=(uint16_t)fg;}
 }
}
/* v1.7 mode 4: two pixels per halfword (VRAM takes no byte writes); dst is
 * the halfword of an even column, 120 halfwords per line. */
void blit_pairs(volatile uint16_t *dst,const uint16_t *cols,unsigned pairs,unsigned fg,unsigned bg){
 const uint16_t lut[4]={(uint16_t)(bg|bg<<8),(uint16_t)(fg|bg<<8),(uint16_t)(bg|fg<<8),(uint16_t)(fg|fg<<8)};   /* v1.8 */
 for(unsigned k=0;k<pairs;k++){
  unsigned ab=cols[2*k]|(unsigned)cols[2*k+1]<<16;volatile uint16_t *d=dst+k;
  for(unsigned j=0;j<16;j++,ab>>=1,d+=120)*d=lut[(ab&1)|(ab>>15&2)];
 }
}
