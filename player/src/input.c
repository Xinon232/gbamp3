/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). 60 Hz button sampler for the v0.8.2 control model. */
#include "input.h"
static uint16_t previous,consumed,frames[10],latched;
void input_poll(uint16_t held){latched|=held;}
void input_reset(uint16_t held_now){previous=held_now;consumed=held_now;for(int i=0;i<10;i++)frames[i]=0;}
void input_consume(uint16_t keys){consumed|=keys&previous;}
/* A repeat tick (delay + k*period, k>=1 for the list case) lies in (a, b]. */
static int crossed(unsigned a,unsigned b,unsigned delay,unsigned period){
 if(b<=delay)return 0;unsigned ka=a>delay?(a-delay)/period:0,kb=(b-delay)/period;
 return kb>ka||(a<=delay&&b>delay&&(b-delay)%period==0);
}
/* Repeat ticks fired up to hold frame h (the press itself not counted). */
static unsigned row_ticks(unsigned h){return h<=INPUT_LIST_DELAY?0:(h-INPUT_LIST_DELAY)/INPUT_ROW_REPEAT;}
static unsigned even_ticks(unsigned h,unsigned period){return h<=INPUT_LIST_DELAY?0:(h-INPUT_LIST_DELAY)/period;}
static uint8_t clamp8(unsigned v){return (uint8_t)(v>255?255:v);}
void input_frame(uint16_t held,unsigned elapsed,struct InputFrame *o){
 /* A key pressed and released entirely between two frames (a long redraw)
  * still counts as one press and one release in this frame. */
 uint16_t brief=latched&~held&~previous;latched=0;
 uint16_t pressed=(held&~previous)|brief,released=(previous&~held)|brief;
 o->held=held;o->pressed=pressed;o->tap=released&~consumed;o->repeat=0;o->lr_chord=0;o->a_chord=0;
 consumed&=held;
 /* Hold counters advance by the video frames elapsed; thresholds that were
  * stepped over this call still fire once. */
 uint16_t before[10];
 for(int i=0;i<10;i++){before[i]=frames[i];if(held&(1u<<i)){unsigned v=frames[i]+elapsed;frames[i]=(uint16_t)(v>0xffff?0xffff:v);}else frames[i]=0;o->hold_frames[i]=frames[i];}
 /* L+R chord (lock): fires once when both are down; neither taps afterwards. */
 if((held&(KEY_L|KEY_R))==(KEY_L|KEY_R)&&(pressed&(KEY_L|KEY_R))){o->lr_chord=1;consumed|=KEY_L|KEY_R;}
 /* UP/DOWN: press plus auto-repeat. */
 for(int b=6;b<=7;b++){uint16_t k=(uint16_t)(1u<<b);
  if(pressed&k)o->repeat|=k;
  else if((held&k)&&frames[b]>INPUT_LIST_DELAY&&crossed(before[b],frames[b],INPUT_LIST_DELAY,INPUT_LIST_REPEAT))o->repeat|=k;}
 /* v1.5 long-list steps. */
 o->elapsed=(uint16_t)(elapsed>0xffff?0xffff:elapsed);
 for(int j=0;j<2;j++){
  int b=6+j;uint16_t k=(uint16_t)(1u<<b);unsigned p=(pressed&k)?1:0,h=frames[b],h0=(pressed&k)?0:before[b];
  unsigned rows=p,letters=p;
  if(held&k){rows+=row_ticks(h)-row_ticks(h0);letters+=even_ticks(h,INPUT_LETTER_REPEAT)-even_ticks(h0,INPUT_LETTER_REPEAT);}
  if(held&KEY_L){o->rows[j]=0;o->letters[j]=clamp8(letters);}else{o->rows[j]=clamp8(rows);o->letters[j]=0;}
  b=5-j;k=(uint16_t)(1u<<b);p=(pressed&k)?1:0;h=frames[b];h0=(pressed&k)?0:before[b];   /* pages[0] Left, pages[1] Right */
  unsigned pages=p;if(held&k)pages+=even_ticks(h,INPUT_PAGE_REPEAT)-even_ticks(h0,INPUT_PAGE_REPEAT);
  o->pages[j]=(held&KEY_L)?0:clamp8(pages);
 }
 /* A held + UP/DOWN: reorder chord; A no longer taps. */
 if((held&KEY_A)&&(o->repeat&(KEY_UP|KEY_DOWN))){o->a_chord=1;consumed|=KEY_A;}
 /* LEFT/RIGHT: hold seeks at the threshold and every repeat interval after. */
 for(int b=4;b<=5;b++){uint16_t k=(uint16_t)(1u<<b);
  if((held&k)&&frames[b]>=INPUT_HOLD&&(before[b]<INPUT_HOLD||crossed(before[b],frames[b],INPUT_HOLD,INPUT_REPEAT))){o->repeat|=k;consumed|=k;}}
 previous=held;
}
