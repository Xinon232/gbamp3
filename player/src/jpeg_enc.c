/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). See jpeg_enc.h. */
#include "jpeg_enc.h"
#include <string.h>

struct Out {uint8_t *p;unsigned n,cap;uint32_t acc;unsigned bits;int bad;};
static void byte(struct Out *o,unsigned v){if(o->n<o->cap)o->p[o->n++]=(uint8_t)v;else o->bad=1;}
static void bytes(struct Out *o,const uint8_t *s,unsigned n){for(unsigned i=0;i<n;i++)byte(o,s[i]);}
/* Entropy-coded bits, MSB first, 0xFF stuffed. */
static void put(struct Out *o,uint32_t v,unsigned n){
 for(unsigned i=n;i-->0;){o->acc=o->acc<<1|(v>>i&1);if(++o->bits==8){byte(o,o->acc&255);if((o->acc&255)==255)byte(o,0);o->acc=0;o->bits=0;}}
}
/* The standard luminance DC table (ITU T.81 K.3), used for all components. */
static const uint8_t DC_BITS[16]={0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint16_t DC_CODE[12]={0x0,0x2,0x3,0x4,0x5,0x6,0xe,0x1e,0x3e,0x7e,0xfe,0x1fe};
static const uint8_t DC_LEN[12]={2,3,3,3,3,3,4,5,6,7,8,9};
static void dc(struct Out *o,int d){
 unsigned a=(unsigned)(d<0?-d:d),cat=0;while(a>>cat)cat++;
 put(o,DC_CODE[cat],DC_LEN[cat]);
 if(cat)put(o,(uint32_t)(d<0?d+(int)(1u<<cat)-1:d),cat);
 put(o,0,1);   /* EOB: the only AC symbol, code 0 */
}
static int clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}

unsigned jpeg_flat_cover(const uint16_t *rgb555,uint8_t *out,unsigned cap){
 struct Out o={out,0,cap,0,0,0};
 static const uint8_t head[]={0xff,0xd8, 0xff,0xe0,0,16,'J','F','I','F',0,1,1,0,0,1,0,1,0,0};
 bytes(&o,head,sizeof(head));
 byte(&o,0xff);byte(&o,0xdb);byte(&o,0);byte(&o,67);byte(&o,0);for(unsigned i=0;i<64;i++)byte(&o,1);   /* DQT: all 1 */
 static const uint8_t sof[]={0xff,0xc0,0,17,8,(JPEG_FLAT_SIDE>>8),(JPEG_FLAT_SIDE&255),(JPEG_FLAT_SIDE>>8),(JPEG_FLAT_SIDE&255),3, 1,0x11,0, 2,0x11,0, 3,0x11,0};
 bytes(&o,sof,sizeof(sof));
 byte(&o,0xff);byte(&o,0xc4);byte(&o,0);byte(&o,31);byte(&o,0x00);bytes(&o,DC_BITS,16);for(unsigned i=0;i<12;i++)byte(&o,i);
 byte(&o,0xff);byte(&o,0xc4);byte(&o,0);byte(&o,20);byte(&o,0x10);byte(&o,1);for(unsigned i=1;i<16;i++)byte(&o,0);byte(&o,0);   /* AC: EOB only */
 static const uint8_t sos[]={0xff,0xda,0,12,3, 1,0x00, 2,0x00, 3,0x00, 0,63,0};
 bytes(&o,sos,sizeof(sos));
 /* Each cover pixel is a JPEG_FLAT_SIDE / 64 square of flat 8x8 blocks:
  * DC = 8 (level - 128), levels in 1/8 steps (JFIF YCbCr). */
 const unsigned rep=JPEG_FLAT_SIDE/64u/8u;int pred[3]={0,0,0};
 for(unsigned by=0;by<64u*rep;by++)for(unsigned bx=0;bx<64u*rep;bx++){
  unsigned v=rgb555[(by/rep)*64u+bx/rep];
  int r=(int)((v&31)*255u+15u)/31,g=(int)((v>>5&31)*255u+15u)/31,b=(int)((v>>10&31)*255u+15u)/31;
  /* 8 x (Y, Cb, Cr) - 1024: Q8 coefficients x 8 = Q11 */
  int c[3]={(19595*r+38470*g+7471*b+4096)>>13,
            (-11059*r-21709*g+32768*b+4096)>>13,
            (32768*r-27439*g-5329*b+4096)>>13};
  c[0]=clamp(c[0]-1024,-1024,1016);c[1]=clamp(c[1],-1024,1016);c[2]=clamp(c[2],-1024,1016);
  for(unsigned k=0;k<3;k++){dc(&o,c[k]-pred[k]);pred[k]=c[k];}
 }
 if(o.bits)put(&o,0x7f,8u-o.bits);   /* pad with ones */
 byte(&o,0xff);byte(&o,0xd9);
 return o.bad?0:o.n;
}
