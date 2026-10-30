/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Playback queue: source list + position + modes.
 * Shuffle uses a seeded bijection of [0, n) (no per-song memory): the chosen
 * song plays first, every other song once, then the pass ends. */
#include "queue.h"
#include <string.h>
unsigned queue_mode;
int (*queue_resolve)(const struct QueueSource *,uint32_t,char *);
static struct QueueSource source;
static uint32_t base,count;           /* current base index, list length */
static uint32_t pass_start,pass_q,seed;
static char overlay[QUEUE_OVERLAY][STORAGE_PATH],current_overlay[STORAGE_PATH];
static unsigned overlay_n,on_overlay,stay,pn_changed;

static uint32_t mix(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
/* Bijection on [0, n) by cycle-walking a keyed permutation of the next power of two. */
static uint32_t permute(uint32_t x,uint32_t n,uint32_t key){
 if(n<2)return 0;
 unsigned bits=1;while((1u<<bits)<n)bits++;uint32_t mask=(1u<<bits)-1,a=(mix(key)|1u)&mask,b=mix(key+1)&mask,c=(mix(key+2)|1u)&mask;
 do{x=(x*a+b)&mask;x^=x>>(bits/2+1);x=(x*c)&mask;x^=x>>(bits/3+1);}while(x>=n);
 return x;
}
static uint32_t shuffle_at(uint32_t q){
 if(!q)return pass_start;
 uint32_t t=permute(q-1,count-1,seed);return t<pass_start?t:t+1;
}
static void new_pass(uint32_t start){pass_start=start;pass_q=0;seed=mix(seed+0x9e3779b9u+start);}
void queue_set(const struct QueueSource *src,uint32_t index){
 memcpy(&source,src,sizeof(source));count=src->kind==QUEUE_SINGLE?1:src->count;
 base=index<count?index:0;if(overlay_n||on_overlay)pn_changed=1;overlay_n=0;on_overlay=0;stay=0;
 new_pass(base);
}
const struct QueueSource *queue_source(void){return &source;}
uint32_t queue_count(void){return source.kind==QUEUE_NONE?0:count;}
uint32_t queue_index(void){return base;}
int queue_on_overlay(void){return on_overlay;}
int queue_path(uint32_t i,char *out){
 if(source.kind==QUEUE_NONE||i>=count)return -1;
 if(source.kind==QUEUE_SINGLE){memcpy(out,source.text,STORAGE_PATH);return 0;}
 return queue_resolve?queue_resolve(&source,i,out):-1;
}
int queue_current(char *out){
 if(on_overlay){memcpy(out,current_overlay,STORAGE_PATH);return 0;}
 return queue_path(base,out);
}
static int take_overlay(char *out){
 if(!overlay_n)return 0;
 memcpy(current_overlay,overlay[0],STORAGE_PATH);
 memmove(overlay[0],overlay[1],(overlay_n-1)*STORAGE_PATH);overlay_n--;on_overlay=1;pn_changed=1;
 memcpy(out,current_overlay,STORAGE_PATH);return 1;
}
/* 1: moved to i; -1: moved to i but it cannot be played (missing playlist file) */
static int go(uint32_t i,char *out){base=i;if(on_overlay)pn_changed=1;on_overlay=0;return queue_path(base,out)?-1:1;}
static int next_once(int manual,char *out){
 if(queue_mode==MODE_REPEAT_ONE&&!manual)return queue_current(out)?0:1;
 if(take_overlay(out))return 1;
 if(stay){stay=0;if(base<count&&queue_mode!=MODE_SHUFFLE)return go(base,out);}
 if(queue_mode==MODE_SHUFFLE){
  if(on_overlay){on_overlay=0;}
  if(pass_q+1<count){pass_q++;return go(shuffle_at(pass_q),out);}
  if(!manual)return 0;
  new_pass(mix(seed)%count);return go(pass_start,out);
 }
 if(base+1<count)return go(base+1,out);
 if(queue_mode==MODE_NORMAL&&!manual)return 0;
 return go(0,out);
}
/* Unplayable entries are skipped; at most one full lap, so a list with nothing
 * playable ends instead of spinning. */
int queue_next(int manual,char *out){
 if(source.kind==QUEUE_NONE||!count)return 0;
 uint32_t keep_base=base,keep_q=pass_q;int keep_ov=on_overlay;
 for(uint32_t tries=0;tries<=count;tries++){int r=next_once(manual,out);if(r>0)return r;if(!r)break;}
 base=keep_base;pass_q=keep_q;on_overlay=keep_ov;   /* nothing playable: stay put */
 return 0;
}
static int prev_once(char *out){
 if(on_overlay)return go(base,out);
 stay=0;
 if(queue_mode==MODE_SHUFFLE){if(!pass_q)return 0;pass_q--;return go(shuffle_at(pass_q),out);}
 if(base)return go(base-1,out);
 /* 11.0: Normal wraps like Repeat All (Left on the first song: the last). */
 if(queue_mode==MODE_REPEAT_ALL||queue_mode==MODE_NORMAL)return go(count-1,out);
 return 0;
}
int queue_prev(char *out){
 if(source.kind==QUEUE_NONE||!count)return 0;
 uint32_t keep_base=base,keep_q=pass_q;int keep_ov=on_overlay;
 for(uint32_t tries=0;tries<=count;tries++){int r=prev_once(out);if(r>0)return r;if(!r)break;}
 base=keep_base;pass_q=keep_q;on_overlay=keep_ov;
 return 0;
}
int queue_restart(char *out){
 if(source.kind==QUEUE_NONE||!count)return 0;
 if(overlay_n)pn_changed=1;
 overlay_n=0;
 int r=queue_mode==MODE_SHUFFLE?(new_pass(mix(seed^0x5bd1e995u)%count),go(pass_start,out)):go(0,out);
 return r<0?queue_next(0,out):r;
}
int queue_play_next(const char *path){
 if(overlay_n==QUEUE_OVERLAY)return -1;
 unsigned k=strlen(path);if(k>=STORAGE_PATH)return -1;
 memmove(overlay[1],overlay[0],overlay_n*STORAGE_PATH);
 unsigned n=strlen(path);if(n>=STORAGE_PATH)return -1;memcpy(overlay[0],path,n+1);overlay_n++;pn_changed=1;return 0;
}
void queue_set_mode(unsigned mode){
 if(mode>=MODE_COUNT)mode=MODE_NORMAL;
 if(mode==MODE_SHUFFLE&&queue_mode!=MODE_SHUFFLE)new_pass(base);
 queue_mode=mode;
}
void queue_playlist_removed(uint32_t id,uint32_t i){
 if(source.kind!=QUEUE_PLAYLIST||source.playlist!=id||!count)return;
 count--;source.count=count;
 if(i<base)base--;
 else if(i==base&&!on_overlay){
  /* The playing song continues; progression resumes at the entry that
   * moved into its place. */
  stay=1;if(base>=count&&count)base=count-1,stay=0;
 }
 if(pass_start>=count)pass_start=count?count-1:0;
}
void queue_playlist_moved(uint32_t id,uint32_t from,uint32_t to){
 if(source.kind!=QUEUE_PLAYLIST||source.playlist!=id)return;
 /* 11.0: entries `from` and `to` were exchanged (neighbours as shown; a
  * playlist may hide a missing entry between them, which stays in place). */
 if(base==from)base=to;else if(base==to)base=from;
}
unsigned queue_pn_changed(void){return pn_changed;}
void queue_pn_saved(void){pn_changed=0;}
unsigned queue_overlay_count(void){return overlay_n;}
char *queue_overlay_entry(unsigned i){return overlay[i<QUEUE_OVERLAY?i:0];}
char *queue_overlay_current(void){return current_overlay;}
void queue_overlay_import(unsigned n,unsigned on){overlay_n=n<=QUEUE_OVERLAY?n:0;on_overlay=on!=0;pn_changed=0;}
void queue_pn_touch(void){pn_changed=1;}
