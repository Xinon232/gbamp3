/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Persistent application state (STATE0/1.DAT). */
#include "state.h"
#include "library.h"
#include "ff.h"
#include <string.h>
extern void disk_index_writes(unsigned);
struct Wire {char magic[8];uint32_t generation;struct AppState s;uint32_t checksum;};
/* v0.9.5-v1.2 layout (GBAST002): no menu order. */
struct AppState2 {uint8_t stereo,search_field,mode,has_song;char last_song[STORAGE_PATH];struct QueueSource queue;uint32_t queue_index;uint8_t boost_on,boost_level,pad[2];};
struct Wire2 {char magic[8];uint32_t generation;struct AppState2 s;uint32_t checksum;};
/* v0.8.x - v0.9.0 layout (GBAST001): the same fields without Volume Boost. */
struct AppState1 {uint8_t stereo,search_field,mode,has_song;char last_song[STORAGE_PATH];struct QueueSource queue;uint32_t queue_index;};
struct Wire1 {char magic[8];uint32_t generation;struct AppState1 s;uint32_t checksum;};
static const char *const names[2]={LIBRARY_DIR "/STATE0.DAT",LIBRARY_DIR "/STATE1.DAT"};
static uint32_t generation;
static unsigned newest;
static uint32_t fnv(const void *p,unsigned n){const uint8_t *b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static struct Wire w;
static FIL f;
static int read_slot(unsigned i,struct Wire *out){
 UINT got=0;if(f_open(&f,names[i],FA_READ)!=FR_OK)return 0;
 int ok=f_read(&f,out,sizeof(*out),&got)==FR_OK;
 f_close(&f);
 if(!ok)return 0;
 if(got==sizeof(*out)&&!memcmp(out->magic,"GBAST003",8)&&out->checksum==fnv(out,sizeof(*out)-4))return 1;
 if(got==sizeof(struct Wire2)){
  const struct Wire2 *prior=(const struct Wire2 *)out;
  if(!memcmp(prior->magic,"GBAST002",8)&&prior->checksum==fnv(prior,sizeof(*prior)-4)){
   memcpy(out->magic,"GBAST003",8);menu_order_reset(&out->s.menus);return 1;
  }
 }
 /* A GBAST001 file from v0.8.x/v0.9.0: keep everything, Volume Boost off. */
 if(got!=sizeof(struct Wire1))return 0;
 const struct Wire1 *old=(const struct Wire1 *)out;
 if(memcmp(old->magic,"GBAST001",8)||old->checksum!=fnv(old,sizeof(*old)-4))return 0;
 memcpy(out->magic,"GBAST003",8);
 out->s.boost_on=out->s.boost_level=0;out->s.auto_update=out->s.song_files=0;
 menu_order_reset(&out->s.menus);
 return 1;
}
int state_load(struct AppState *s){
 memset(s,0,sizeof(*s));s->stereo=1;s->flags=STATE_HOLD_RESET_DONE;menu_order_reset(&s->menus);
 static struct Wire b;int va=read_slot(0,&w),vb=read_slot(1,&b);
 if(!va&&!vb){generation=0;newest=1;return 0;}
 newest=vb&&(!va||b.generation>w.generation);
 const struct Wire *x=newest?&b:&w;generation=x->generation;*s=x->s;
 s->last_song[STORAGE_PATH-1]=0;s->queue.text[STORAGE_PATH-1]=0;
 if(s->mode>=MODE_COUNT)s->mode=MODE_NORMAL;if(s->search_field>3)s->search_field=0;
 if(s->boost_on>1||s->boost_level>3){s->boost_on=0;s->boost_level=0;}
 if(s->auto_update>1)s->auto_update=0;
 if(s->song_files>1)s->song_files=0;
 if(s->song_types>2)s->song_types=0;if(s->new_playlist_top>31)s->new_playlist_top=0;s->flags&=0xfe;if(((s->flags>>3)&3)==3)s->flags&=(uint8_t)~0x18u;if(((s->flags>>5)&3)==3)s->flags&=(uint8_t)~0x60u;   /* bits 1-2 file operations, 3-4 Letter Box, 5-6 conversion, 7 the 11.0 Hold START reset is done (new_playlist_top bit 1 holds that switch, set = Off; bit 2, 11.0: + Add Songs on top) */
 /* 11.0: Hold START to unlock is On once for every card, also where it was
  * switched Off before (10.0 and the v2.2 test build cleared bit 7 on
  * loading); a later Off is saved with bit 7 set and kept. */
 if(!(s->flags&STATE_HOLD_RESET_DONE)){s->new_playlist_top&=(uint8_t)~2u;s->flags|=STATE_HOLD_RESET_DONE;}
 if(!menu_order_validate(&s->menus))menu_order_reset(&s->menus);
 menu_order_upgrade(&s->menus);   /* v1.6: Show songs as */
 return 1;
}
int state_save(const struct AppState *s){
 unsigned slot=newest^1;
 memset(&w,0,sizeof(w));memcpy(w.magic,"GBAST003",8);w.generation=generation+1;w.s=*s;w.checksum=fnv(&w,sizeof(w)-4);
 disk_index_writes(1);
 int e=library_system_dir();UINT got=0;
 if(!e){if(library_open(&f,names[slot],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)e=-1;
  else{if(f_write(&f,&w,sizeof(w),&got)!=FR_OK||got!=sizeof(w)||f_sync(&f)!=FR_OK)e=-1;if(f_close(&f)!=FR_OK)e=-1;}}
 disk_index_writes(0);
 if(!e){generation++;newest=slot;}
 return e;
}

/* ---- Play Next list (v0.9.5) ---- */
static const char *const pn_names[2]={LIBRARY_DIR "/QUEUE0.DAT",LIBRARY_DIR "/QUEUE1.DAT"};
static uint32_t pn_generation;
static unsigned pn_newest=1;
int pn_delete(void){
 int bad=0;disk_index_writes(1);
 for(unsigned i=0;i<2;i++){
  FRESULT r=f_unlink(pn_names[i]);if(r!=FR_OK&&r!=FR_NO_FILE&&r!=FR_NO_PATH)bad=1;
 }
 disk_index_writes(0);
 if(!bad){pn_generation=0;pn_newest=1;}
 return bad?-1:0;
}
int state_delete_configuration(void){
 /* Explicit, non-recursive whitelist: never traverse directories or remove
  * PLIST slots / Pxxxxxxxx.DAT playlists, music, or the system folder. */
 int bad=pn_delete()!=0;disk_index_writes(1);
 for(unsigned i=0;i<2;i++){
  FRESULT r=f_unlink(names[i]);if(r!=FR_OK&&r!=FR_NO_FILE&&r!=FR_NO_PATH)bad=1;
 }
 disk_index_writes(0);
 if(!bad){generation=0;newest=1;}
 return bad?-1:0;
}
static uint32_t fnv_more(uint32_t h,const void *p,unsigned n){const uint8_t *b=p;while(n--)h=(h^*b++)*16777619u;return h;}
struct PnHead {char magic[8];uint32_t generation,flags,count;};
static int pn_write(FIL *o,const void *p,unsigned n,uint32_t *h){UINT got=0;*h=fnv_more(*h,p,n);return f_write(o,p,n,&got)==FR_OK&&got==n?0:-1;}
int pn_save(void){
 unsigned slot=pn_newest^1,n=queue_overlay_count(),on=queue_on_overlay();
 struct PnHead h;memcpy(h.magic,"GBAPN001",8);h.generation=pn_generation+1;h.flags=on?1:0;h.count=n;
 disk_index_writes(1);
 int e=library_system_dir();
 if(!e){
  if(library_open(&f,pn_names[slot],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)e=-1;
  else{
   uint32_t sum=2166136261u;
   e=pn_write(&f,&h,sizeof(h),&sum);
   if(!e&&on){const char *c=queue_overlay_current();e=pn_write(&f,c,strlen(c)+1,&sum);}
   for(unsigned i=0;i<n&&!e;i++){const char *c=queue_overlay_entry(i);e=pn_write(&f,c,strlen(c)+1,&sum);}
   UINT got=0;if(!e&&(f_write(&f,&sum,4,&got)!=FR_OK||got!=4||f_sync(&f)!=FR_OK))e=-1;
   if(f_close(&f)!=FR_OK)e=-1;
  }
 }
 disk_index_writes(0);
 if(!e){pn_generation++;pn_newest=slot;queue_pn_saved();}
 return e;
}
/* One NUL-terminated path at the file position into dst (< STORAGE_PATH). */
static int pn_path(char *dst,uint32_t *sum){
 FSIZE_t at=f_tell(&f);UINT got=0;
 if(f_read(&f,dst,STORAGE_PATH,&got)!=FR_OK||!got)return -1;
 unsigned k=0;while(k<got&&dst[k])k++;
 if(k>=got)return -1;                                 /* no NUL within 256 bytes */
 *sum=fnv_more(*sum,dst,k+1);
 return f_lseek(&f,at+k+1)==FR_OK?0:-1;
}
/* Validates slot i; with load, also fills the queue arrays. Returns the
 * count, or -1. */
static int pn_slot(unsigned i,int load,struct PnHead *h,unsigned *on){
 /* Validation reads into the (still empty) queue arrays too: no extra RAM. */
 char *scratch=queue_overlay_entry(QUEUE_OVERLAY-1);UINT got=0;int r=-1;
 if(f_open(&f,pn_names[i],FA_READ)!=FR_OK)return -1;
 if(f_read(&f,h,sizeof(*h),&got)==FR_OK&&got==sizeof(*h)&&!memcmp(h->magic,"GBAPN001",8)&&h->count<=QUEUE_OVERLAY&&h->flags<=1){
  uint32_t sum=fnv_more(2166136261u,h,sizeof(*h)),stored=0;int bad=0;
  if(h->flags&1)bad=pn_path(load?queue_overlay_current():scratch,&sum);
  for(unsigned k=0;k<h->count&&!bad;k++)bad=pn_path(load?queue_overlay_entry(k):scratch,&sum);
  if(!bad&&f_read(&f,&stored,4,&got)==FR_OK&&got==4&&stored==sum&&f_tell(&f)==f_size(&f)){r=(int)h->count;*on=h->flags&1;}
 }
 f_close(&f);return r;
}
unsigned pn_load(unsigned *on){
 struct PnHead a,b;unsigned oa=0,ob=0;*on=0;
 int va=pn_slot(0,0,&a,&oa),vb=pn_slot(1,0,&b,&ob);
 if(va<0&&vb<0){pn_generation=0;pn_newest=1;return 0;}
 pn_newest=vb>=0&&(va<0||b.generation>a.generation);
 struct PnHead *x=pn_newest?&b:&a;pn_generation=x->generation;
 int n=pn_slot(pn_newest,1,x,on);
 if(n<0){*on=0;return 0;}
 return (unsigned)n;
}
