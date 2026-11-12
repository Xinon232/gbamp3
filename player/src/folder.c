/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Streaming directory listing under 0:/gbamp3.
 *
 * 12.0. Until 11.0 opening a folder read it four times (folders counted,
 * songs counted, and once more per kind for the sort keys), the audio was
 * fed only after 32 entries read in one call, and a call ended at every
 * song (never, in a folder of songs); a row was found again by reading up
 * to 32 or more entries from a checkpoint. With music playing that gave
 * underruns in Folders (a SuperCard reads a sector in about a millisecond).
 * Now:
 * - one pass fills every cursor bound to the folder (the list's folders
 *   and songs, the queue's songs): counts, sort records, places;
 * - folder_yield() runs before every entry read and every few steps of
 *   the sort, whatever the entries are;
 * - a sort record ends in the entry's place in the folder instead of its
 *   number, so a row is one seek and one f_readdir. The order is the same:
 *   the same folded key, then the order on the card;
 * - the pass reads the folder's sectors itself (pass_raw). f_readdir takes
 *   1.6 ms of processor time per long-named entry on FAT32 and 2.7 ms on
 *   exFAT (code in ROM), far more than the card: 5000 songs were 8 to 13
 *   seconds per pass. The pass takes what the listing needs (attributes,
 *   the name's end, the first characters for the key) and hands every
 *   entry that is not plain to f_readdir (short names with other than
 *   ASCII, surrogate pairs, names over 255 bytes), a folder it cannot
 *   follow wholly (pass_slow). Rows are still read with f_readdir. */
#include "folder.h"
#include <string.h>
#include "text.h"
#include "diskio.h"
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
static int wanted(unsigned kind,const FILINFO *f){
 if(!f->fname[0]||f->fname[0]=='.'||(f->fattrib&(AM_HID|AM_SYS)))return 0;
 if(f->fattrib&AM_DIR)return kind==FOLDER_DIRS&&!folder_system(f->fname);
 return kind==FOLDER_SONGS&&folder_is_song(f->fname);
}
int folder_join(char *out,const char *dir,const char *name){
 unsigned a=strlen(dir),b=strlen(name);if(a+1+b>=STORAGE_PATH)return 0;
 memcpy(out,dir,a);out[a]='/';memcpy(out+a+1,name,b+1);return 1;
}
#define NOPOS 0xffffffffu
/* Cursors that lend scratch: a pass fills all of them that are bound to the folder. */
static struct DirCursor *reg[FOLDER_CURSORS];static unsigned reg_n;
void folder_bind(struct DirCursor *c,const char *path,unsigned kind){
 if(c->open){f_closedir(&c->dir);c->open=0;}
 unsigned n=strlen(path);if(n>=STORAGE_PATH)n=STORAGE_PATH-1;
 memcpy(c->path,path,n);c->path[n]=0;c->kind=kind;c->counted=c->count=c->marks=0;c->pos=NOPOS;c->stride=FOLDER_STRIDE;c->sort_state=0;
 c->clus_n=c->clus_shift=0;c->gen++;c->loading=0;
}
void folder_sort_area(struct DirCursor *c,uint8_t *buf,unsigned cap){
 c->sort=buf;c->sort_cap=cap;c->sort_state=0;
 for(unsigned i=0;i<reg_n;i++)if(reg[i]==c)return;
 if(reg_n<FOLDER_CURSORS)reg[reg_n++]=c;
}
static void stale(struct DirCursor *c){c->counted=0;c->sort_state=0;c->pos=NOPOS;}
/* The song folder_name() read last for the queue: where it is on the card
 * (folder_keep), so that it is opened without a search for its name
 * (folder_open). FatFs looks a name up entry by entry: 0.7 to 1.4 s of
 * processor time for a song in a folder of 2000, twice per song (tags,
 * audio), at every song change. */
static struct {DIR dir;char path[STORAGE_PATH];DWORD clust;FSIZE_t size;BYTE attr,stat,ok;} hint;
void folder_changed(void){hint.ok=0;for(unsigned i=0;i<reg_n;i++){stale(reg[i]);reg[i]->gen++;}}
/* exFAT folders can be longer than 65536 entries (2 MiB): places are kept
 * in pairs of entries there. The entry before an odd place is the last one
 * of the entry set before it, never the start of a set, so reading from
 * the even place gives the same entry. */
static unsigned place_shift(const struct DirCursor *c){return c->dir.obj.fs->fs_type==FS_EXFAT;}
static int fixed_root(const DIR *d){return !d->obj.sclust&&d->obj.fs->fs_type<FS_FAT32;}
/* Remember the cluster number of the folder's cluster k. */
static void note(struct DirCursor *c,uint32_t k,uint32_t clust){
 while((k>>c->clus_shift)>=FOLDER_CLUSTERS){   /* full: keep every other cluster */
  for(unsigned j=0;j<FOLDER_CLUSTERS/2;j++)c->clus[j]=c->clus[2*j];
  c->clus_shift++;c->clus_n=(c->clus_n+1)/2;
 }
 if(k&((1u<<c->clus_shift)-1))return;
 k>>=c->clus_shift;
 while(c->clus_n<=k)c->clus[c->clus_n++]=0;
 c->clus[k]=clust;
}
static FILINFO f;
/* Move the read position to place P: straight there when its cluster is
 * known, else by reading on from the nearest known cluster before it.
 * `slack`: places the walk may end beyond P (exFAT pairs). */
static int seek(struct DirCursor *c,unsigned g,uint32_t P,unsigned slack){
 DIR *d=&c->dir;FATFS *fs=d->obj.fs;uint32_t at;
 if(fixed_root(d)){at=P;d->clust=0;d->sect=fs->dirbase+at/16;}
 else{
  uint32_t per=(uint32_t)fs->csize*16,ci=P/per;unsigned j=ci>>c->clus_shift;
  if(!c->clus_n)return -1;
  if(j>=c->clus_n)j=c->clus_n-1;
  while(j&&!c->clus[j])j--;
  if(c->clus[j]<2)return -1;
  uint32_t base=(uint32_t)j<<c->clus_shift;
  at=base==ci?P:base*per;
  if(d->sect&&d->dptr/32<=P&&d->dptr/32>=at)goto walk;   /* already nearer */
  d->clust=c->clus[j];d->sect=fs->database+(LBA_t)fs->csize*(d->clust-2)+(at%per)/16;
 }
 d->dptr=at*32;d->dir=fs->win+(at%16)*32;
walk:
 while(d->dptr/32<P){
  folder_yield();if(c->gen!=g)return -1;
  if(f_readdir(d,&f)!=FR_OK||!f.fname[0]||!d->sect)return -1;
 }
 return d->dptr/32-P<=slack?0:-1;
}
static void take(char *name,unsigned cap){unsigned k=strlen(f.fname);if(k>=cap)k=cap-1;memcpy(name,f.fname,k);name[k]=0;}
/* Advance to the next entry of the cursor's kind; *name filled when non-null. 1 found, 0 end. */
static int step(struct DirCursor *c,unsigned g,char *name,unsigned cap){
 for(;;){
  /* Yield before reading: the hook may list another folder through `f`. */
  folder_yield();if(c->gen!=g)return -1;
  if(f_readdir(&c->dir,&f)!=FR_OK)return -1;
  if(!f.fname[0])return 0;
  if(!wanted(c->kind,&f))continue;
  if(name)take(name,cap);
  c->pos++;return 1;
 }
}
/* ---- one pass: counts, marks, clusters, sort records or places ---- */
/* Records are an even number of bytes at even addresses: moved as 16-bit words, upwards. */
static inline void move(uint8_t *d,const uint8_t *s,unsigned n){uint16_t *x=(uint16_t*)(void*)d;const uint16_t *y=(const uint16_t*)(const void*)s;for(n/=2;n--;)*x++=*y++;}
/* The key of a record cut to a shorter record, as fold_key() would have
 * made it: whole characters only, zeros after them. */
static void repack(struct DirCursor *c,unsigned n,unsigned rec){
 unsigned old=c->sort_rec;
 for(unsigned i=0;i<n;i++){
  uint8_t *s=c->sort+i*old,*d=c->sort+i*rec,cut=s[rec-2],p0=s[old-2],p1=s[old-1];
  move(d,s,rec-2);
  if((cut&0xc0)==0x80){unsigned k=rec-2;while(k&&(d[k-1]&0xc0)==0x80)d[--k]=0;if(k)d[--k]=0;}
  d[rec-2]=p0;d[rec-1]=p1;
 }
}
/* The name of the entry the raw pass is at: UTF-16, as FatFs gathers it. */
static uint16_t lfn[FF_MAX_LFN+1];
/* One more entry of the cursor's kind at place P. name: UTF-8, or null:
 * the first `len` characters of lfn[] are the name. */
static void add(struct DirCursor *c,uint32_t P,unsigned sh,const char *name,unsigned len){
 unsigned n=c->count;
 /* A checkpoint before entry n when it starts a stride. */
 if(n&&!(n&(c->stride-1))){   /* the stride is a power of two */
  if(n/c->stride>FOLDER_MARKS){   /* full: keep every other mark, double the stride */
   for(unsigned j=0;j<FOLDER_MARKS/2;j++)c->mark[j]=c->mark[2*j+1];
   c->marks=FOLDER_MARKS/2;c->stride*=2;
  }
  if(n%c->stride==0&&n/c->stride==c->marks+1)c->mark[c->marks++]=P;
 }
 uint32_t q=P>>sh;
 if(c->tier&&q>0xffff)c->tier=0;   /* a place does not fit: the marks serve */
 if(c->tier==1){
  /* The record size the whole folder will have, as far as it is known
   * (it shrinks when the count passes rec_max: no division per entry). */
  unsigned rec=c->sort_rec;
  if(n+1>c->rec_max){rec=c->sort_cap/(n+1);if(rec>32)rec=32;rec&=~1u;}
  if(rec<4){   /* too many for records: keep the places, in card order */
   uint16_t *t=(uint16_t*)(void*)c->sort;
   for(unsigned i=0;i<n;i++){const uint8_t *r=c->sort+i*c->sort_rec+c->sort_rec-2;uint16_t v=(uint16_t)(r[0]<<8|r[1]);t[i]=v;}
   c->tier=2;c->tab_stride=1;
  }else{
   if(rec<c->sort_rec)repack(c,n,rec);
   c->sort_rec=rec;c->rec_max=c->sort_cap/rec;
   uint8_t *r=c->sort+n*rec;
   if(name)fold_key((char*)r,rec-2,name);
   else{
    /* The key holds at most rec-2 characters: fold only the name's start,
     * the whole name when that did not fill the key (dropped accents). */
    /* fold_key() on the name, from its UTF-16 (one unit is one code point: the pass leaves surrogates to f_readdir). */
    struct Fold fo={0,0};unsigned cap=rec-2,k=0;
    for(unsigned i=0;i<len;i++){uint32_t ch=lfn[i];
     if(ch<0x80){if(k>=cap)break;if(ch>='A'&&ch<='Z')ch+=32;fo.latin=ch>='a'&&ch<='z';r[k++]=(uint8_t)ch;continue;}
     if(!(ch=fold_point(&fo,ch)))continue;
     size_t w=utf8_put((char*)r+k,cap-k,ch);if(!w)break;k+=w;}
    for(;k<cap;k++)r[k]=0;
   }
   r[rec-2]=(uint8_t)(q>>8);r[rec-1]=(uint8_t)q;
  }
 }
 if(c->tier==2&&!(n&(c->tab_stride-1))){
  uint16_t *t=(uint16_t*)(void*)c->sort;unsigned j=n/c->tab_stride;
  if((j+1)*2>c->sort_cap){for(unsigned k=0;k<(j+1)/2;k++)t[k]=t[2*k];c->tab_stride*=2;}
  if(!(n&(c->tab_stride-1)))t[n/c->tab_stride]=(uint16_t)q;
 }
 c->count=n+1;
}
/* Records: folded key then the big-endian place, so a plain memcmp orders
 * by name and keeps the order on the card among equal keys. */
/* Heapsort, bottom-up: the hole goes down to a leaf along the larger
 * children (one comparison a level), then the record climbs back to its
 * place. The result is the one order of the records (no two are equal:
 * the places differ). For the sort the two bytes of every 16-bit word of
 * the records are swapped: then comparing words as numbers is memcmp on
 * the bytes, and a record's last word is its place. They stay that way
 * (folder_name reads the place; nothing else reads sorted records). */
static unsigned work;
static uint16_t held[16];
#define W(i) (base+(i)*rw)
static void sift(uint16_t *base,unsigned rw,unsigned i,unsigned n){
 unsigned j=i,k;
 for(;;){unsigned l=2*j+1;if(l>=n)break;work++;
  if(l+1<n){const uint16_t *a=W(l),*b=W(l+1);for(k=0;k<rw&&a[k]==b[k];k++){}if(k<rw&&a[k]<b[k])l++;}
  {uint16_t *d=W(j);const uint16_t *s=W(l);for(k=0;k<rw;k++)d[k]=s[k];}
  j=l;}
 while(j>i){unsigned p=(j-1)/2;const uint16_t *a=W(p);
  for(k=0;k<rw&&a[k]==held[k];k++){}
  if(k==rw||a[k]>held[k])break;   /* the parent is not smaller */
  {uint16_t *d=W(j);for(k=0;k<rw;k++)d[k]=a[k];}
  j=p;}
 {uint16_t *d=W(j);for(k=0;k<rw;k++)d[k]=held[k];}
}
#define SORT_WORK 24   /* sift levels between two yields (about a millisecond) */
static int order(struct DirCursor *c,unsigned g){
 unsigned n=c->count,rw=c->sort_rec/2,k;uint16_t *base=(uint16_t*)(void*)c->sort;
 for(unsigned i=0;i<n*rw;i++){unsigned v=base[i];base[i]=(uint16_t)(v<<8|v>>8);if(!(i&1023)){folder_yield();if(c->gen!=g)return 1;}}
 for(unsigned i=n/2;i--;){for(k=0;k<rw;k++)held[k]=W(i)[k];sift(base,rw,i,n);if(work>=SORT_WORK){work=0;folder_yield();if(c->gen!=g)return 1;}}
 for(unsigned e=n;e>1;){e--;for(k=0;k<rw;k++){held[k]=W(e)[k];W(e)[k]=base[k];}sift(base,rw,0,e);if(work>=SORT_WORK){work=0;folder_yield();if(c->gen!=g)return 1;}}
 return 0;
}
#undef W
/* ---- the pass over the folder's own sectors ---- */
static uint8_t raw_sec[512],raw_fat[512],xset[19*32];static LBA_t raw_fat_at;static unsigned raw_gen;
/* A sector FatFs holds in its window comes from there (it may be newer than the card's). */
static int rd(FATFS *fs,LBA_t s,uint8_t *b){
 if(s==fs->winsect){memcpy(b,fs->win,512);return 0;}
 return disk_read(fs->pdrv,b,s,1)==RES_OK?0:-1;
}
struct Raw {FATFS *fs;const DIR *d;uint32_t clst,idx,per;LBA_t sect;unsigned ex;};
/* Entry r->idx: 1 and *e, 0 the folder ends, -1 read error, 2 not followed here (f_readdir decides). */
static int raw_entry(struct Raw *r,const uint8_t **e){
 FATFS *fs=r->fs;uint32_t i=r->idx;
 if(!(i&15)){
  if(i){
   if(i>=(r->ex?0x10000000u:0x200000u)/32)return 0;
   if(i&(r->per-1))r->sect++;   /* a cluster is a power of two of sectors */
   else{
    uint32_t c=r->clst,v;const FFOBJID *o=&r->d->obj;
    if(r->ex&&(FSIZE_t)i*32>=o->objsize)return 0;   /* gbaSD: an exFAT folder ends at its length */
    if(r->ex&&o->stat==2){
     uint32_t cofs=c-o->sclust,clen=(uint32_t)((LBA_t)((o->objsize-1)/512)/fs->csize);
     if(cofs>=clen)return cofs==clen?0:2;
     v=c+1;
    }else{
     unsigned w=fs->fs_type==FS_FAT16?2:4;LBA_t at=fs->fatbase+c/(512/w);const uint8_t *p;
     if(at==fs->winsect)p=fs->win;
     else{if(at!=raw_fat_at){raw_fat_at=~(LBA_t)0;if(rd(fs,at,raw_fat))return -1;raw_fat_at=at;}p=raw_fat;}
     p+=c*w%512;
     v=w==2?(uint32_t)(p[0]|p[1]<<8):((uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24)&(r->ex?0x7fffffffu:0x0fffffffu);
    }
    if(v<2)return 2;
    if(v>=fs->n_fatent)return 0;
    r->clst=v;r->sect=fs->database+(LBA_t)fs->csize*(v-2);
   }
  }
  if(rd(fs,r->sect,raw_sec))return -1;
 }
 *e=raw_sec+(i&15)*32;return 1;
}
/* One long-name entry into lfn[], as FatFs's pick_lfn() takes it. *hi: all
 * its characters or-ed; *len: the name's length, from the entry that ends
 * it; *odd: a 0 inside the name (FatFs would cut it there). */
static int pick(const uint8_t *e,unsigned *hi,unsigned *len,unsigned *odd){
 static const uint8_t at[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 if(e[26]|e[27])return 0;
 unsigned i=((e[0]&~0x40u)-1)*13,wc=1,last=e[0]&0x40;
 for(unsigned s=0;s<13;s++){
  unsigned uc=e[at[s]]|(unsigned)e[at[s]+1]<<8;
  if(wc){
   if(i>=FF_MAX_LFN+1)return 0;
   lfn[i++]=(uint16_t)(wc=uc);
   if(uc)*hi|=uc;else if(last)*len=i-1;else *odd=1;
  }else if(uc!=0xffff)return 0;
 }
 if(last&&wc){if(i>=FF_MAX_LFN+1)return 0;lfn[i]=0;*len=i;}
 return 1;
}
/* The usual part of a long name: 13 characters, none 0, not the name's last part. 0: pick() decides. */
static int pick13(const uint8_t *e,unsigned i,unsigned *hi){
 uint16_t *o=lfn+i;unsigned h=0,c;
#define CH(k) c=e[k]|(unsigned)e[k+1]<<8;if(!c)return 0;*o++=(uint16_t)c;h|=c;
 CH(1)CH(3)CH(5)CH(7)CH(9)CH(14)CH(16)CH(18)CH(20)CH(22)CH(24)CH(28)CH(30)
#undef CH
 *hi|=h;return 1;
}
/* An entry FatFs would return, its name in lfn[0..len): each cursor that lists its kind takes it. */
static void emit(struct DirCursor **cs,unsigned n,uint32_t P,unsigned sh,unsigned attr,unsigned len){
 if(lfn[0]=='.'||(attr&(AM_HID|AM_SYS)))return;
 unsigned kind=FOLDER_DIRS;
 if(attr&AM_DIR){
  static const char sys[]=FOLDER_SYSTEM;unsigned i=0;
  if(len==sizeof(sys)-1){while(i<len&&(lfn[i]|32u)==((unsigned)sys[i]|32u))i++;if(i==len)return;}
 }else{
  kind=FOLDER_SONGS;
  if(len<5||lfn[len-4]!='.')return;
  unsigned a=lfn[len-3]|32u,b=lfn[len-2]|32u,c=lfn[len-1]|32u;
  if(!(((folder_song_mask&1)&&a=='m'&&b=='p'&&c=='3')||((folder_song_mask&2)&&a=='g'&&b=='a'&&c=='c')))return;
 }
 for(unsigned i=0;i<n;i++)if(cs[i]->kind==kind)add(cs[i],P,sh,0,len);
}
/* lfn[0..len) is a name the pass may take itself: no 0, no surrogate, at most 255 bytes of UTF-8. */
static int plain_name(unsigned len,unsigned hi){
 if(!len)return 0;
 if(hi<0x80)return 1;
 /* hi: every character or-ed. A surrogate has the bits d800; without one, 85 characters are at most 255 bytes. */
 if((hi&0xd800)!=0xd800&&len<=FF_LFN_BUF/3)return 1;
 unsigned bytes=0;
 for(unsigned i=0;i<len;i++){unsigned c=lfn[i];if(!c||(c&0xf800)==0xd800)return 0;bytes+=c<0x80?1:c<0x800?2:3;}
 return bytes<=FF_LFN_BUF;
}
/* The entry that starts at place P, through f_readdir. 1 taken, else the pass's result. */
static int punt(struct DirCursor **cs,unsigned n,const unsigned *g,uint32_t P,unsigned sh){
 struct DirCursor *a=cs[0];
 if(seek(a,g[0],P,0))return a->gen!=g[0]?1:2;
 if(f_readdir(&a->dir,&f)!=FR_OK)return -1;
 if(!f.fname[0])return 2;
 for(unsigned i=0;i<n;i++)if(wanted(cs[i]->kind,&f))add(cs[i],P,sh,f.fname,0);
 return 1;
}
/* 0 done, -1 read error, 1 a cursor was taken over, 2 not followed here
 * (pass_slow), 3 the pass's buffers were used by a pass inside folder_yield
 * (again). */
static int pass_raw(struct DirCursor **cs,unsigned n,const unsigned *g,unsigned sh){
 struct DirCursor *a=cs[0];const DIR *d=&a->dir;FATFS *fs=d->obj.fs;unsigned ex=fs->fs_type==FS_EXFAT;
 if(!d->obj.sclust||(fs->fs_type!=FS_FAT16&&fs->fs_type!=FS_FAT32&&!ex))return 2;
 if(ex&&(!d->obj.objsize||(d->obj.stat!=2&&(d->obj.stat||d->obj.n_frag))))return 2;
 if(d->clust<2||d->clust>=fs->n_fatent||!d->sect)return 2;
 struct Raw r={fs,d,d->clust,0,(uint32_t)fs->csize*16,d->sect,ex};
 unsigned mine=++raw_gen,ord=0xff,sum=0,hi=0,len=0,odd=0;uint32_t start=0,fed=~0u;   /* start: the place after the last entry returned */
 const uint8_t *e=0;
 raw_fat_at=~(LBA_t)0;
 for(;;r.idx++){
  int k;
  if(r.idx>>4!=fed){   /* a new sector: feed the audio first */
   fed=r.idx>>4;folder_yield();
   for(unsigned i=0;i<n;i++)if(cs[i]->gen!=g[i])return 1;
   if(raw_gen!=mine)return 3;
  }
  if(r.idx&15)e+=32;   /* the next entry of the sector */
  else{
   if((k=raw_entry(&r,&e))!=1)return k;
   if(!(r.idx&(r.per-1)))for(unsigned i=0;i<n;i++)note(cs[i],r.idx/r.per,r.clst);
  }
  unsigned b=e[0];
  if(!b)return 0;
  if(ex){
   if(b!=0x85)continue;
   /* An entry set: file, stream, names (FatFs's load_xdir()). */
   unsigned sz=(e[1]+1u)*32;
   if(sz<3*32||sz>19*32)return 2;
   memcpy(xset,e,32);
   for(unsigned off=32;off<sz;off+=32){
    r.idx++;
    if((k=raw_entry(&r,&e))!=1)return k<0?k:2;
    if(!(r.idx&(r.per-1)))for(unsigned i=0;i<n;i++)note(cs[i],r.idx/r.per,r.clst);
    if(e[0]!=(off==32?0xc0:0xc1))return 2;
    memcpy(xset+off,e,32);
    if(off==32&&(xset[35]+44u)/15*32>sz)return 2;
   }
   unsigned x=0;
   for(unsigned i=0;i<sz;i++){if(i==2)i++;else x=((x&1)?0x8000u:0)+(x>>1)+xset[i];x&=0xffff;}
   if(x!=(xset[2]|(unsigned)xset[3]<<8))return 2;   /* a torn set: f_readdir reports it */
   len=xset[35];hi=0;
   for(unsigned i=0,si=64;i<len;i++){if(!(si&31))si+=2;unsigned c=xset[si]|(unsigned)xset[si+1]<<8;si+=2;lfn[i]=(uint16_t)c;hi|=c;if(!c)hi|=0xd800;}
   if(plain_name(len,hi))emit(cs,n,start,sh,xset[4]&0x37u,len);
   else{if((k=punt(cs,n,g,start,sh))!=1)return k;if(raw_gen!=mine)return 3;}
   start=r.idx+1;
   continue;
  }
  unsigned attr=e[11]&0x3fu;
  if(b==0xe5||b=='.'||(attr&~0x20u)==0x08){ord=0xff;continue;}
  if(attr==0x0f){   /* a part of a long name (FatFs's dir_read()) */
   if(b&0x40){sum=e[13];b&=~0x40u;ord=b;hi=len=odd=0;}
   ord=b==ord&&sum==e[13]&&((b<=19&&e[0]==b&&!(e[26]|e[27])&&pick13(e,(b-1)*13,&hi))||pick(e,&hi,&len,&odd))?ord-1:0xff;
   continue;
  }
  /* The entry itself. */
  unsigned s8=0,ok=0;
  for(unsigned i=0;i<11;i++)s8=((s8>>1)+(s8<<7)+e[i])&0xff;
  if(!ord&&sum==s8)ok=!odd&&plain_name(len,hi);   /* its long name */
  else{   /* its short name, as get_fileinfo() writes it */
   unsigned di=0,lcf=0x08;ok=1;
   for(unsigned si=0;si<11;){unsigned c=e[si++];
    if(c<0x20||c>0x7e||c=='.'){ok=0;break;}
    if(c==' ')continue;
    if(si==9){lfn[di++]='.';lcf=0x10;}
    if(c>='A'&&c<='Z'&&(e[12]&lcf))c+=32;
    lfn[di++]=(uint16_t)c;}
   if(!di)ok=0;
   len=di;
  }
  if(ok)emit(cs,n,start,sh,attr,len);
  else{if((k=punt(cs,n,g,start,sh))!=1)return k;if(raw_gen!=mine)return 3;}
  ord=0xff;start=r.idx+1;
 }
}
/* The pass through f_readdir (FAT12, a folder the raw pass gave up on). */
static int pass_slow(struct DirCursor **cs,unsigned n,const unsigned *g,unsigned sh){
 struct DirCursor *a=cs[0];
 for(;;){
  folder_yield();
  for(unsigned i=0;i<n;i++)if(cs[i]->gen!=g[i])return 1;
  uint32_t P=a->dir.dptr/32;
  if(!fixed_root(&a->dir))for(unsigned i=0;i<n;i++)note(cs[i],P/((uint32_t)a->dir.obj.fs->csize*16),a->dir.clust);
  if(f_readdir(&a->dir,&f)!=FR_OK)return -1;
  if(!f.fname[0])return 0;
  for(unsigned i=0;i<n;i++)if(wanted(cs[i]->kind,&f))add(cs[i],P,sh,f.fname,0);
 }
}
static void empty(struct DirCursor *c){
 c->counted=c->count=c->marks=0;c->stride=FOLDER_STRIDE;c->pos=NOPOS;c->clus_n=c->clus_shift=0;
 c->sort_state=0;c->tier=!folder_sort_busy&&c->sort&&c->sort_cap>=4;c->sort_rec=32;c->rec_max=c->sort_cap/32;c->tab_stride=1;
}
unsigned folder_slow;   /* tests: 1 reads every folder through f_readdir (0 at the start: no initialised data is relied on) */
/* 0 done, -1 read error, 1 another pass (made from inside folder_yield)
 * took a cursor over: what it filled stands, the others are read again. */
static int scan(struct DirCursor **cs,unsigned n){
 struct DirCursor *a=cs[0];unsigned g[FOLDER_CURSORS];int r=3;
 if(a->open){f_closedir(&a->dir);a->open=0;}
 if(f_opendir(&a->dir,a->path)!=FR_OK)return -1;
 a->open=1;
 unsigned sh=place_shift(a);
 for(unsigned i=0;i<n;i++){g[i]=++cs[i]->gen;cs[i]->loading=1;}
 for(unsigned tries=0;r>=2;tries++){
  for(unsigned i=0;i<n;i++)empty(cs[i]);
  if(tries&&f_readdir(&a->dir,0)!=FR_OK){r=-1;break;}   /* back to the start */
  r=!folder_slow&&tries<3?pass_raw(cs,n,g,sh):2;
  if(r==2){
   for(unsigned i=0;i<n;i++)empty(cs[i]);
   if(f_readdir(&a->dir,0)!=FR_OK){r=-1;break;}
   r=pass_slow(cs,n,g,sh);
  }
 }
 for(unsigned i=0;i<n&&!r;i++){struct DirCursor *c=cs[i];
  if(c!=a){if(c->open)f_closedir(&c->dir);c->dir=a->dir;c->open=1;}
  c->counted=1;}
 for(unsigned i=0;i<n&&!r;i++){struct DirCursor *c=cs[i];
  if(c->gen!=g[i])continue;   /* taken over while another one was sorted */
  if(c->tier==1&&order(c,g[i])){r=1;break;}
  if(c->sort&&!folder_sort_busy){c->sort_state=c->tier==1?1:c->tier==2?3:2;c->sort_epoch=folder_sort_epoch;}
 }
 for(unsigned i=0;i<n;i++){struct DirCursor *c=cs[i];
  if(c->gen!=g[i])continue;
  c->loading=0;
  if(r&&!(c->counted&&c->sort_state))stale(c);}
 return r;
}
static int need(const struct DirCursor *c){
 return !c->counted||(c->sort&&!folder_sort_busy&&(!c->sort_state||c->sort_epoch!=folder_sort_epoch));
}
/* Another cursor already holds this folder's listing of the same kind
 * (the list's songs when a song is played from it): copy it. */
static int adopt(struct DirCursor *c){
 if(!c->sort||folder_sort_busy)return 0;
 for(unsigned i=0;i<reg_n;i++){struct DirCursor *d=reg[i];
  if(d==c||d->loading||need(d)||d->kind!=c->kind||d->sort_cap!=c->sort_cap||!d->open||strcmp(d->path,c->path))continue;
  if(c->open)f_closedir(&c->dir);
  c->dir=d->dir;c->open=1;c->pos=NOPOS;c->count=d->count;c->counted=1;c->marks=d->marks;c->stride=d->stride;
  memcpy(c->mark,d->mark,sizeof(c->mark));memcpy(c->clus,d->clus,sizeof(c->clus));c->clus_n=d->clus_n;c->clus_shift=d->clus_shift;
  c->tier=d->tier;c->tab_stride=d->tab_stride;c->sort_rec=d->sort_rec;c->sort_state=d->sort_state;c->sort_epoch=d->sort_epoch;
  unsigned used=d->sort_state==1?d->count*d->sort_rec:d->sort_state==3?((d->count+d->tab_stride-1)/d->tab_stride)*2:0;
  memcpy(c->sort,d->sort,used);c->gen++;
  return 1;
 }
 return 0;
}
static int load(struct DirCursor *c){
 for(unsigned tries=0;tries<3;tries++){
  if(adopt(c))return 0;
  struct DirCursor *cs[FOLDER_CURSORS];unsigned n=1;cs[0]=c;
  for(unsigned i=0;i<reg_n;i++){struct DirCursor *m=reg[i];
   if(m!=c&&!m->loading&&m->path[0]&&need(m)&&!strcmp(m->path,c->path))cs[n++]=m;}
  int r=scan(cs,n);
  if(r<0)return -1;
  if(!r||!need(c))return 0;
 }
 return -1;
}
void folder_prepare(struct DirCursor *c){if(c->path[0]&&need(c))(void)load(c);}
int folder_count(struct DirCursor *c){
 if(!c->counted&&load(c))return -1;
 return (int)c->count;
}
int folder_count_path(const char *path,unsigned kind){
 static DIR d;int n=0;
 if(f_opendir(&d,path)!=FR_OK)return -1;
 for(;;){
  folder_yield();
  if(f_readdir(&d,&f)!=FR_OK){n=-1;break;}
  if(!f.fname[0])break;
  if(wanted(kind,&f))n++;
 }
 f_closedir(&d);return n;
}
int folder_sorted(struct DirCursor *c){
 if(folder_sort_busy)return 0;
 if(need(c)&&load(c))return 0;
 return c->sort_state==1||(c->counted&&c->count<2);
}
int folder_name(struct DirCursor *c,unsigned index,char *name,unsigned cap){
 if((!c->counted||need(c))&&load(c))return -1;
 if(index>=c->count)return -1;
 if(!c->open){if(f_opendir(&c->dir,c->path)!=FR_OK)return -1;c->open=1;c->pos=NOPOS;}
 unsigned g=c->gen,sh=place_shift(c),mine=c->sort&&!folder_sort_busy&&c->sort_epoch==folder_sort_epoch;   /* else: scratch in use */
 if(mine&&c->sort_state==1){
  uint32_t q=((const uint16_t*)(const void*)c->sort)[(index+1)*(c->sort_rec/2)-1];   /* the record's last word */
  if(seek(c,g,q<<sh,sh))return -1;
  c->pos=NOPOS;
  if(f_readdir(&c->dir,&f)!=FR_OK||!wanted(c->kind,&f)){stale(c);return -1;}   /* the folder changed */
  take(name,cap);return 0;
 }
 /* Card order: from the nearest kept place before the entry. */
 unsigned cp=0,slack=0;uint32_t P=0;
 if(mine&&c->sort_state==3){unsigned j=index/c->tab_stride;cp=j*c->tab_stride;P=(uint32_t)((const uint16_t*)(const void*)c->sort)[j]<<sh;slack=sh;}
 else{unsigned m=index/c->stride;if(m>c->marks)m=c->marks;if(m){cp=m*c->stride;P=c->mark[m-1];}}
 if(c->pos==NOPOS||c->pos>index||c->pos<cp){if(seek(c,g,P,slack))return -1;c->pos=cp;}
 while(c->pos<index){if(step(c,g,0,0)!=1)return -1;}
 return step(c,g,name,cap)==1?0:-1;
}
/* 12.0: the folded sort key of entry `index` (UTF-8, as fold_key() made it
 * from the name, cut to whole characters) without a card read: the letter
 * steps (L + Up / Down) look up about 20 rows each. -1: not kept (folder
 * not sorted, the scratch lent to Update Library, or an empty key). */
int folder_key(struct DirCursor *c,unsigned index,char *out,unsigned cap){
 if(folder_sort_busy||c->sort_state!=1||c->sort_epoch!=folder_sort_epoch||!c->counted||index>=c->count||cap<2)return -1;
 const uint16_t *r=(const uint16_t*)(const void*)c->sort+index*(c->sort_rec/2);unsigned n=c->sort_rec-2,k=0;
 if(n>cap-1)n=cap-1;
 for(;k<n;k++){unsigned v=r[k/2];out[k]=(char)(k&1?v:v>>8);}   /* the words were swapped for the sort */
 out[n]=0;
 return out[0]?0:-1;
}
void folder_keep(struct DirCursor *c,const char *path){
 unsigned n=strlen(path),k=strlen(f.fname);
 hint.ok=0;
 if(!c->open||n>=STORAGE_PATH||k>=n||path[n-k-1]!='/'||strcmp(path+n-k,f.fname)||(f.fattrib&AM_DIR))return;   /* `f` is not this song */
 memcpy(hint.path,path,n+1);hint.dir=c->dir;hint.clust=f.fclust;hint.size=f.fsize;hint.attr=f.fattrib;hint.stat=f.fstat;hint.ok=1;
}
FRESULT folder_open(FIL *fp,const char *path){
 if(hint.ok&&!strcmp(path,hint.path)){
  f.fattrib=hint.attr;f.fclust=hint.clust;f.fsize=hint.size;f.fstat=hint.stat;
  if(f_open_entry(fp,&hint.dir,&f)==FR_OK)return FR_OK;
 }
 return f_open(fp,path,FA_READ);
}
