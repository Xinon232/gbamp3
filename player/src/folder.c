/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Streaming directory listing under 0:/gbamp3. */
#include "folder.h"
#include <string.h>
#include "text.h"
unsigned folder_sort_epoch,folder_sort_busy;
static void no_yield(void){}
void (*folder_yield)(void)=no_yield;
static int ext(const char *s,const char *e){unsigned n=strlen(s);return n>=5&&s[n-4]=='.'&&(s[n-3]|32)==e[0]&&(s[n-2]|32)==e[1]&&(s[n-1]|32)==e[2];}
unsigned folder_song_mask=3;   /* v1.8 Show Songs: bit 0 MP3, bit 1 GAC */
int folder_is_song(const char *name){return ((folder_song_mask&1)&&ext(name,"mp3"))||((folder_song_mask&2)&&ext(name,"gac"));}
/* The data folder by name, any case (it is no longer hidden). */
int folder_system(const char *name){
 const char *s=FOLDER_SYSTEM;
 for(;*s;s++,name++)if((*name|32)!=(*s|32))return 0;
 return !*name;
}
static int wanted(const struct DirCursor *c,const FILINFO *f){
 if(!f->fname[0]||f->fname[0]=='.'||(f->fattrib&(AM_HID|AM_SYS)))return 0;
 if(f->fattrib&AM_DIR)return c->kind==FOLDER_DIRS&&!folder_system(f->fname);
 return c->kind==FOLDER_SONGS&&folder_is_song(f->fname);
}
int folder_join(char *out,const char *dir,const char *name){
 unsigned a=strlen(dir),b=strlen(name);if(a+1+b>=STORAGE_PATH)return 0;
 memcpy(out,dir,a);out[a]='/';memcpy(out+a+1,name,b+1);return 1;
}
void folder_bind(struct DirCursor *c,const char *path,unsigned kind){
 if(c->open){f_closedir(&c->dir);c->open=0;}
 unsigned n=strlen(path);if(n>=STORAGE_PATH)n=STORAGE_PATH-1;
 memcpy(c->path,path,n);c->path[n]=0;c->kind=kind;c->counted=c->count=c->pos=c->marks=0;c->stride=FOLDER_STRIDE;c->sort_state=0;
}
void folder_sort_area(struct DirCursor *c,uint8_t *buf,unsigned cap){c->sort=buf;c->sort_cap=cap;c->sort_state=0;}
static int rewind_to(struct DirCursor *c,unsigned index){
 unsigned m=index/c->stride;if(m>c->marks)m=c->marks;
 if(!m&&c->open){f_closedir(&c->dir);c->open=0;}
 if(!c->open){if(f_opendir(&c->dir,c->path)!=FR_OK)return -1;c->open=1;}
 c->pos=0;if(!m)return 0;
 /* Same folder: the object part of `dir` is unchanged, only the position moves. */
 const struct FolderMark *k=&c->mark[m-1];
 c->dir.dptr=k->dptr;c->dir.clust=k->clust;c->dir.sect=k->sect;c->dir.dir=k->at;c->pos=m*c->stride;return 0;
}
/* Advance to the next wanted entry; *name filled when non-null. 1 found, 0 end. */
static FILINFO f;
static int step(struct DirCursor *c,char *name,unsigned cap){
 for(unsigned n=0;;n++){
  /* Record a checkpoint before reading entry pos when it starts a stride. */
  if(c->pos&&c->pos%c->stride==0&&c->pos/c->stride>c->marks){
   if(c->marks==FOLDER_MARKS){   /* full: keep every other mark, double the stride */
    for(unsigned j=0;j<FOLDER_MARKS/2;j++)c->mark[j]=c->mark[2*j+1];
    c->marks=FOLDER_MARKS/2;c->stride*=2;
   }
   if(c->pos%c->stride==0&&c->pos/c->stride==c->marks+1){struct FolderMark *k=&c->mark[c->marks++];k->dptr=c->dir.dptr;k->clust=c->dir.clust;k->sect=c->dir.sect;k->at=c->dir.dir;}
  }
  /* Yield before reading: the hook may list another folder through `f`. */
  if((n&31)==31)folder_yield();
  if(f_readdir(&c->dir,&f)!=FR_OK)return -1;
  if(!f.fname[0])return 0;
  if(!wanted(c,&f))continue;
  if(name){unsigned k=strlen(f.fname);if(k>=cap)k=cap-1;memcpy(name,f.fname,k);name[k]=0;}
  c->pos++;return 1;
 }
}
int folder_count(struct DirCursor *c){
 if(c->counted)return (int)c->count;
 if(rewind_to(c,0))return -1;
 int r;while((r=step(c,0,0))==1){}
 if(r<0)return -1;
 c->count=c->pos;c->counted=1;return (int)c->count;
}
static int raw_name(struct DirCursor *c,unsigned index,char *name,unsigned cap){
 if(c->counted&&index>=c->count)return -1;
 if(!c->open||index<c->pos||index/c->stride>c->pos/c->stride+1){if(rewind_to(c,index))return -1;}
 while(c->pos<index){int r=step(c,0,0);if(r!=1)return -1;}
 return step(c,name,cap)==1?0:-1;
}
/* Records: folded key then big-endian u16 directory index, so a plain memcmp
 * orders by name and keeps directory order among equal keys. */
static void swap_rec(uint8_t *a,uint8_t *b,unsigned n){while(n--){uint8_t t=*a;*a++=*b;*b++=t;}}
static void sift(uint8_t *base,unsigned rec,unsigned i,unsigned n){
 for(;;){unsigned l=2*i+1,m=i;
  if(l<n&&memcmp(base+l*rec,base+m*rec,rec)>0)m=l;
  if(l+1<n&&memcmp(base+(l+1)*rec,base+m*rec,rec)>0)m=l+1;
  if(m==i)return;swap_rec(base+i*rec,base+m*rec,rec);i=m;}
}
static char sort_name[STORAGE_PATH];
static void make_order(struct DirCursor *c){
 c->sort_state=2;c->sort_epoch=folder_sort_epoch;
 if(!c->sort||folder_count(c)<2)return;
 unsigned n=c->count,rec=c->sort_cap/n;if(rec>32)rec=32;rec&=~1u;
 if(n>65535||rec<4)return;          /* too large: directory order */
 if(rewind_to(c,0))return;
 for(unsigned i=0;i<n;i++){
  if(step(c,sort_name,sizeof(sort_name))!=1)return;
  uint8_t *r=c->sort+i*rec;fold_key((char*)r,rec-2,sort_name);r[rec-2]=(uint8_t)(i>>8);r[rec-1]=(uint8_t)i;
 }
 for(unsigned i=n/2;i--;)sift(c->sort,rec,i,n);
 for(unsigned e=n;e>1;){e--;swap_rec(c->sort,c->sort+e*rec,rec);sift(c->sort,rec,0,e);if(!(e&63))folder_yield();}
 c->sort_rec=rec;c->sort_state=1;
}
int folder_sorted(struct DirCursor *c){
 if(folder_sort_busy)return 0;
 if(c->sort&&(!c->sort_state||c->sort_epoch!=folder_sort_epoch))make_order(c);
 return c->sort_state==1||(c->counted&&c->count<2);
}
int folder_name(struct DirCursor *c,unsigned index,char *name,unsigned cap){
 if(folder_sort_busy)return raw_name(c,index,name,cap);   /* scratch in use */
 if(c->sort&&(!c->sort_state||c->sort_epoch!=folder_sort_epoch))make_order(c);
 if(c->sort_state==1){
  if(index>=c->count)return -1;
  const uint8_t *r=c->sort+index*c->sort_rec+c->sort_rec-2;index=(unsigned)r[0]<<8|r[1];
 }
 return raw_name(c,index,name,cap);
}
