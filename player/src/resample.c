/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). See resample.h. */
#include "resample.h"
#include "resample_tables.h"
#if defined(__arm__)
uint64_t rs_dot2(const int16_t *h,const int16_t *l,const int16_t *r);   /* gac_enc_arm.S */
#endif
void resample_begin(struct Resampler *r,unsigned rate){
 r->base=7;r->frac=0;r->rate=rate;r->num=0;r->den=1;r->rem=0;
 r->tab=rate==44100?RS_44100:rate==48000?RS_48000:RS_UP;
}
void resample_begin_down(struct Resampler *r){
 r->base=7;r->frac=0;r->rate=RS_DOWN_STEP;r->tab=RS_DOWN;r->num=54;r->den=125;r->rem=0;
}
unsigned resample_run(struct Resampler *r,const int16_t *L,const int16_t *R,uint32_t avail,int flush,int16_t *out,unsigned max){
 unsigned n=0;uint32_t base=r->base,frac=r->frac,rate=r->rate;
 while(n<max&&(flush?base<avail:base+8<avail)){
  const int16_t *h=r->tab[frac>>7];int32_t l=8192,rr=8192;
  if(base+8<avail){const int16_t *pl=L+base-7,*pr=R+base-7;
   #if defined(__arm__)
   uint64_t v=rs_dot2(h,pl,pr);l=(int32_t)(uint32_t)v;rr=(int32_t)(uint32_t)(v>>32);
   #else
   for(unsigned k=0;k<RS_TAPS;k++){l+=h[k]*pl[k];rr+=h[k]*pr[k];}
   #endif
  }
  else for(unsigned k=0;k<RS_TAPS;k++){uint32_t i=base-7+k;if(i<avail){l+=h[k]*L[i];rr+=h[k]*R[i];}}
  l>>=14;rr>>=14;
  out[2*n]=(int16_t)(l<-32768?-32768:l>32767?32767:l);out[2*n+1]=(int16_t)(rr<-32768?-32768:rr>32767?32767:rr);n++;
  frac+=rate;if(r->num){r->rem+=r->num;if(r->rem>=r->den){r->rem-=r->den;frac++;}}
  base+=frac>>15;frac&=32767;
 }
 r->base=base;r->frac=frac;return n;
}
