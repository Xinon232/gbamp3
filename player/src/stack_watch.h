/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). */
#ifndef STACK_WATCH_H
#define STACK_WATCH_H
#include <stdint.h>
struct StackWatch { unsigned low, cursor, size; };
static inline void stack_watch_init(struct StackWatch*s,volatile const uint32_t*p,unsigned n,uint32_t paint){s->size=n;s->cursor=0;unsigned i=0;while(i<n&&p[i]==paint)i++;s->low=i;}
/* Historical observed low-water, never increased by restored paint.
 * Guard and adjacent downward growth are checked every call. Sparse damage
 * in the remaining prefix is revisited within ceil(initial low/64) calls.
 * This is diagnostics, not a stack-overflow prevention mechanism. */
static inline void stack_watch_update(struct StackWatch*s,volatile const uint32_t*p,uint32_t paint){
 if(!s->low)return;
 if(p[0]!=paint){s->low=0;s->cursor=0;return;}
 while(s->low&&p[s->low-1]!=paint)s->low--;
 for(unsigned n=0;n<64&&s->low;n++){
  if(s->cursor>=s->low)s->cursor=0;
  unsigned at=s->cursor++;
  if(p[at]!=paint)s->low=at;
 }
}
#endif
