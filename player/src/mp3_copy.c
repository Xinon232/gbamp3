/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). v0.9.8 (Part B1a): word-sized copies for Helix's
 * bit-reservoir handling in mp3dec.c (built with -Dmemcpy=mp3_memcpy
 * -Dmemmove=mp3_memmove; no Helix source change). The generic memcpy runs
 * a byte loop from ROM (about 26 cycles per byte); these run as ARM from the
 * MP3 decoder overlay and move whole words, merging shifted words when source
 * and destination alignments differ. mp3_memmove only ever moves data towards
 * the buffer start (dst <= src); anything else falls back to a byte loop. */
#include <stddef.h>
#include <stdint.h>
#define FAST __attribute__((section(".iwram"),target("arm"),noinline))
void FAST *mp3_memcpy(void *dst,const void *src,size_t n){
 uint8_t *d=dst;const uint8_t *s=src;
 while(n&&((uintptr_t)d&3)){*d++=*s++;n--;}
 unsigned o=(uintptr_t)s&3;
 if(n>=8){
  uint32_t *dw=(uint32_t*)d;
  if(!o){const uint32_t *sw=(const uint32_t*)s;while(n>=16){uint32_t a=sw[0],b=sw[1],c=sw[2],e=sw[3];dw[0]=a;dw[1]=b;dw[2]=c;dw[3]=e;dw+=4;sw+=4;n-=16;}
   while(n>=4){*dw++=*sw++;n-=4;}s=(const uint8_t*)sw;}
  else{
   /* Aligned reads of the source words; each output word joins two of them.
    * The last read ends inside the source word that holds byte s+n-1. */
   const uint32_t *sw=(const uint32_t*)(s-o);unsigned lo=8*o,hi=32-lo;uint32_t w0=*sw++;
   while(n>=8){uint32_t w1=*sw++;*dw++=w0>>lo|w1<<hi;w0=w1;n-=4;s+=4;}
  }
  d=(uint8_t*)dw;
 }
 while(n--)*d++=*s++;
 return dst;
}
void FAST *mp3_memmove(void *dst,const void *src,size_t n){
 if((uintptr_t)dst<=(uintptr_t)src)return mp3_memcpy(dst,src,n);
 uint8_t *d=(uint8_t*)dst+n;const uint8_t *s=(const uint8_t*)src+n;while(n--)*--d=*--s;return dst;
}
