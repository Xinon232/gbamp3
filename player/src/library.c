/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GBA-built music library, index v7 (no song cap).
 *
 * Slot file layout (LIB0.IDX / LIB1.IDX in GBAMP3.SYS), little endian:
 *   sector 0   header (written last; checksum)
 *   records    variable length, 40-byte head + six NUL-terminated strings
 *   offsets    u32 per song: record offset relative to rec_off
 *   perms      PERM_COUNT arrays of u32 record ids, `count` each
 *   groups     GROUP_COUNT tables of 20-byte LibGroup entries
 *   cover map  u32 per song: album cover index, 0xffffffff none (v4)
 *   covers     COVER_REC bytes each, one per album: 64x64 colour numbers
 *              and at most COVER_COLORS RGB555 colours (v7, see cover.h)
 *   providers  u32 per cover: the record id whose picture it is (v4)
 *   words      v1.2 title word index (see "word search" below): u32 record
 *              ids, u8 word byte offsets, u8 duplicate bytes and the first
 *              WORD_PRE key bytes, `wrd_count` each, sorted by the folded
 *              title from the word
 * Update Library walks /gbamp3, reuses unchanged records, then builds every
 * permutation with an on-card merge sort of 128-byte key records, then one
 * cover per album from the first song of the album with a usable picture
 * (an MP3's embedded JPEG via jpeg_dc.c, or a GAC's own cover), reduced to
 * its colours, then the title word index. v2.1: only a v7 index is read
 * (the v6 sections with reduced covers); Update Library builds one. */
#include "library.h"
#include "folder.h"
#include "text.h"
#include "ff.h"
#include "jpeg_dc.h"
#include "cover.h"
#include "gac4.h"
#include "exfat_repair.h"
#include <string.h>
extern void disk_index_writes(unsigned);

#define HEAD_BYTES 512
#define REC_MAX 1024
#define KEY_BYTES 124
#define SORT_REC 128
#define SORT_BYTES 65536
#define SORT_CAP (SORT_BYTES/SORT_REC)
#define GROUP_BYTES 20
#define REC_HEAD 40
#define COVER_BYTES COVER_REC          /* v7: reduced 64x64 */
#define NO_COVER 0xffffffffu
#define WORD_KEY 122     /* v1.2 word entries: folded title from the word on */
#define WORD_MAX 32      /* word starts indexed per title */
#define WORD_PRE 8       /* key bytes stored per entry: short queries need no record read */
#define WORD_PASS PERM_COUNT   /* build_perm() pass number of the word index */
#ifndef WORD_ENTRY_CAP   /* merge limit: SORT_CAP runs of SORT_CAP records */
#define WORD_ENTRY_CAP ((uint32_t)SORT_CAP*SORT_CAP)
#endif
#define HEADER_COMMON \
 char magic[8]; \
 uint32_t generation,count,complete,rec_off,rec_len,off_off; \
 uint32_t perm_off[PERM_COUNT],grp_off[GROUP_COUNT],grp_count[GROUP_COUNT]; \
 uint32_t cov_map_off,cov_img_off,cov_count,cov_src_off;
struct Header {
 HEADER_COMMON
 uint32_t wrd_count,wrd_id_off,wrd_pos_off,wrd_dup_off,wrd_pre_off;   /* v5 (v1.2) */
 uint32_t checksum;
};
static const char *const slot_name[2]={LIBRARY_DIR "/LIB0.IDX",LIBRARY_DIR "/LIB1.IDX"};
static FIL src;
static unsigned src_open,active;
static struct Header head;
/* One remembered (result index, entry) pair: stepping one row or one song
 * costs a few dup bytes instead of a scan from the range start. */
static struct {uint32_t lo,hi,qb,gen,i,e,valid;} wmemo;
static uint8_t sortbuf[SORT_BYTES] __attribute__((aligned(4)));

static uint32_t fnv(uint32_t h,const void *p,unsigned n){const uint8_t *b=p;while(n--)h=(h^*b++)*16777619u;return h;}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}

/* ---- read cache over the active slot ---- */
static uint8_t cache[4][512];
static uint32_t cache_sec[4]={~0u,~0u,~0u,~0u};
static unsigned cache_age[4],cache_clock;
static void cache_reset(void){for(int i=0;i<4;i++)cache_sec[i]=~0u;}
static int src_read(uint32_t pos,void *out,unsigned n){
 uint8_t *o=out;
 while(n){
  uint32_t sec=pos/512;unsigned off=pos%512,k=512-off<n?512-off:n,slot=4;
  for(unsigned i=0;i<4;i++)if(cache_sec[i]==sec)slot=i;
  if(slot==4){slot=0;for(unsigned i=1;i<4;i++)if(cache_age[i]<cache_age[slot])slot=i;
   UINT got=0;if(f_lseek(&src,sec*512)!=FR_OK||f_read(&src,cache[slot],512,&got)!=FR_OK)return -1;
   if(got<512)memset(cache[slot]+got,0,512-got);cache_sec[slot]=sec;}
  cache_age[slot]=++cache_clock;memcpy(o,cache[slot]+off,k);o+=k;pos+=k;n-=k;
 }
 return 0;
}
static int valid_header(struct Header *h,FSIZE_t size){
 if(memcmp(h->magic,"GBALIB7",8)||h->checksum!=fnv(2166136261u,h,sizeof(*h)-4))return 0;
 if(!h->generation)return 0;
 uint64_t c=h->count;
 if(h->rec_off!=HEAD_BYTES||h->off_off<h->rec_off+h->rec_len||h->off_off%4)return 0;
 for(unsigned p=0;p<PERM_COUNT;p++)if(h->perm_off[p]!=h->off_off+c*4*(p+1))return 0;
 uint64_t at=h->perm_off[PERM_COUNT-1]+c*4;
 for(unsigned g=0;g<GROUP_COUNT;g++){if(h->grp_off[g]!=at)return 0;at+=(uint64_t)h->grp_count[g]*GROUP_BYTES;}
 if(h->cov_map_off!=at||h->cov_img_off!=at+c*4||h->cov_count>c)return 0;
 at+=c*4+(uint64_t)h->cov_count*COVER_BYTES;
 if(h->cov_src_off!=at)return 0;
 at+=(uint64_t)h->cov_count*4;
 uint64_t w=h->wrd_count;
 if(h->wrd_id_off!=at||h->wrd_pos_off!=at+w*4||h->wrd_dup_off!=at+w*5||h->wrd_pre_off!=at+w*6)return 0;
 at+=w*(6+WORD_PRE);
 return at<=size;
}
static FIL dst,tmp_off,run_a,grp_tmp[GROUP_COUNT];
static int read_header(unsigned slot,struct Header *h){
 FIL *f=&tmp_off;   /* update-only handle, idle here: keeps 550 bytes off the stack */
 if(f_open(f,slot_name[slot],FA_READ)!=FR_OK)return 0;
 UINT got=0;int ok=f_read(f,h,sizeof(*h),&got)==FR_OK&&got==sizeof(*h)&&valid_header(h,f_size(f));
 f_close(f);return ok;
}
int library_init(void){
 if(src_open){f_close(&src);src_open=0;}
 cache_reset();memset(&head,0,sizeof(head));wmemo.valid=0;
 struct Header a,b;int va=read_header(0,&a),vb=read_header(1,&b);
 if(!va&&!vb)return 0;
 active=vb&&(!va||b.generation>a.generation);head=active?b:a;
 if(f_open(&src,slot_name[active],FA_READ)!=FR_OK){memset(&head,0,sizeof(head));return -1;}
 /* Fast seek: a cluster link map makes random record reads O(1) instead of
  * walking the FAT chain from the file start (falls back if too fragmented). */
 static DWORD clmt[64];clmt[0]=64;src.cltbl=clmt;
 if(f_lseek(&src,CREATE_LINKMAP)!=FR_OK)src.cltbl=0;
 src_open=1;return 0;
}
int library_delete_index(void){
 /* No directory traversal, no temporary/playlist/music files. A failed
  * unlink is not reported as a completed reset. Reopen any surviving slot. */
 if(src_open){if(f_close(&src)!=FR_OK)return -1;src_open=0;}
 int bad=0;disk_index_writes(1);
 for(unsigned i=0;i<2;i++){
  FRESULT r=f_unlink(slot_name[i]);if(r!=FR_OK&&r!=FR_NO_FILE&&r!=FR_NO_PATH)bad=1;
 }
 disk_index_writes(0);
 if(library_init())bad=1;
 return bad?-1:0;
}
unsigned library_count(void){return src_open?head.count:0;}
unsigned library_generation(void){return src_open?head.generation:0;}

/* ---- records ---- */
/* Head: u16 len, u8 format, u8 channels, u32 size, date, duration_ms, rate,
 * u16 track, disc, u8 flags (1 art, 2 title from name, 4 VBR), u8 pad,
 * u16 kbps, u32 checksum, u32 picture offset, u32 picture length. */
static int decode(const uint8_t *r,unsigned len,struct LibSong *s){
 if(len<REC_HEAD||len>REC_MAX||len%4)return -1;
 uint8_t tmp[4];memcpy(tmp,r+28,4);uint32_t want=le32(tmp);
 uint32_t h=fnv(2166136261u,r,28);h=fnv(h,"\0\0\0\0",4);h=fnv(h,r+32,len-32);
 if(h!=want)return -1;
 s->format=r[2];s->channels=r[3];s->size=le32(r+4);s->date=le32(r+8);s->duration_ms=le32(r+12);s->rate=le32(r+16);
 s->track=(uint16_t)(r[20]|r[21]<<8);s->disc=(uint16_t)(r[22]|r[23]<<8);s->has_art=r[24]&1;s->title_from_name=(r[24]>>1)&1;
 s->vbr=(r[24]>>2)&1;s->bitrate=(uint16_t)(r[26]|r[27]<<8);s->pic_off=le32(r+32);s->pic_len=le32(r+36);
 char *dst[6]={s->path+10,s->title,s->artist,s->album,s->album_artist,s->genre};
 unsigned cap[6]={STORAGE_PATH-10,META_TITLE,META_NAME,META_NAME,META_NAME,META_GENRE},at=REC_HEAD;
 memcpy(s->path,"0:/gbamp3/",10);
 for(unsigned i=0;i<6;i++){unsigned n=0;while(at+n<len&&r[at+n])n++;if(at+n>=len||n>=cap[i])return -1;memcpy(dst[i],r+at,n);dst[i][n]=0;at+=n+1;}
 return 0;
}
static unsigned encode(uint8_t *r,const char *rel,const struct SongMeta *m,uint32_t size,uint32_t date){
 memset(r,0,REC_MAX);
 r[2]=m->format;r[3]=m->channels;put32(r+4,size);put32(r+8,date);put32(r+12,m->duration_ms);put32(r+16,m->rate);
 r[20]=(uint8_t)m->track;r[21]=(uint8_t)(m->track>>8);r[22]=(uint8_t)m->disc;r[23]=(uint8_t)(m->disc>>8);
 r[24]=(uint8_t)((m->has_art?1:0)|(m->title_from_name?2:0)|(m->vbr?4:0));r[26]=(uint8_t)m->bitrate;r[27]=(uint8_t)(m->bitrate>>8);
 put32(r+32,m->pic_off);put32(r+36,m->pic_len);
 const char *src_s[6]={rel,m->title,m->artist,m->album,m->album_artist,m->genre};unsigned at=REC_HEAD;
 for(unsigned i=0;i<6;i++){unsigned n=strlen(src_s[i]);if(at+n+1>REC_MAX-4)n=0;memcpy(r+at,src_s[i],n);at+=n+1;}
 at=(at+3)&~3u;r[0]=(uint8_t)at;r[1]=(uint8_t)(at>>8);
 uint32_t h=fnv(2166136261u,r,28);h=fnv(h,"\0\0\0\0",4);h=fnv(h,r+32,at-32);put32(r+28,h);
 return at;
}
static uint8_t rec_tmp[REC_MAX];
/* Update-walk record buffer. Idle outside an update and during a pass's
 * merge; v1.2 search and the word pass borrow it (EWRAM is nearly full). */
static uint8_t rec[REC_MAX];
static int read_record_at(uint32_t pos,uint8_t *r,unsigned *len){
 if(src_read(pos,r,32))return -1;unsigned n=(unsigned)r[0]|(unsigned)r[1]<<8;
 if(n<REC_HEAD||n>REC_MAX)return -1;if(src_read(pos+32,r+32,n-32))return -1;*len=n;return 0;
}
static int record_pos(uint32_t id,uint32_t *pos){
 uint8_t b[4];if(!src_open||id>=head.count||src_read(head.off_off+4*id,b,4))return -1;
 *pos=head.rec_off+le32(b);return 0;
}
int library_song(uint32_t id,struct LibSong *s){
 uint32_t pos;unsigned len;if(record_pos(id,&pos)||read_record_at(pos,rec_tmp,&len))return -1;
 if(decode(rec_tmp,len,s))return -1;s->id=id;return 0;
}
int library_perm(unsigned perm,uint32_t i,uint32_t *id){
 uint8_t b[4];if(!src_open||perm>=PERM_COUNT||i>=head.count||src_read(head.perm_off[perm]+4*i,b,4))return -1;
 *id=le32(b);return *id<head.count?0:-1;
}
unsigned library_groups(unsigned k){return src_open&&k<GROUP_COUNT?head.grp_count[k]:0;}
int library_group(unsigned k,uint32_t i,struct LibGroup *g){
 uint8_t b[GROUP_BYTES];if(k>=GROUP_COUNT||i>=library_groups(k)||src_read(head.grp_off[k]+GROUP_BYTES*i,b,GROUP_BYTES))return -1;
 g->start=le32(b);g->count=le32(b+4);g->rep=le32(b+8);g->sub_start=le32(b+12);g->sub_count=le32(b+16);return 0;
}
/* v1.8: out may be video memory: read in pieces, stored in halfwords.
 * v2.1: the reduced record (cover.h). */
int library_cover(uint32_t id,volatile uint16_t *out){
 uint8_t b[64];if(!src_open||id>=head.count||src_read(head.cov_map_off+4*id,b,4))return -1;
 uint32_t k=le32(b);if(k>=head.cov_count)return -1;
 uint64_t at=head.cov_img_off+(uint64_t)k*COVER_BYTES;
 for(unsigned i=0;i<COVER_BYTES;i+=64){if(src_read(at+i,b,64))return -1;cover_copy16(out+i/2,b,64);}
 return 0;
}
const char *library_artist(const char *s){return s&&s[0]?s:"Unknown Artist";}
const char *library_album(const char *s){return s&&s[0]?s:"Unknown Album";}
const char *library_genre(const char *s){return s&&s[0]?s:"Unknown Genre";}

/* ---- search ---- */
static struct LibSong probe_song;
static const char *basename(const char *p){const char *b=p;for(;*p;p++)if(*p=='/')b=p+1;return b;}
static const char *field_of(unsigned f,const struct LibSong *s){
 return f==FIELD_FILENAME?basename(s->path):f==FIELD_ALBUM?s->album:f==FIELD_ARTIST?s->artist:s->title;
}
static int cmp_at(unsigned perm,unsigned field,uint32_t i,const char *q,int *out){
 uint32_t id;if(library_perm(perm,i,&id)||library_song(id,&probe_song))return -1;
 const char *v=field_of(field,&probe_song);
 /* Unknown (empty) artist/album sort last and never match a query. */
 *out=v[0]?fold_compare(v,q,1):1;return 0;
}
int library_prefix(unsigned field,const char *q,unsigned *perm,uint32_t *lo,uint32_t *hi){
 unsigned p=field==FIELD_FILENAME?PERM_FILE:field==FIELD_ALBUM?PERM_ALBUM:field==FIELD_ARTIST?PERM_ARTIST:PERM_TITLE;
 *lo=0;*hi=library_count();
 if(!q[0]){*perm=PERM_TITLE;return 0;}
 *perm=p;
 uint32_t a=0,b=library_count();int c;
 while(a<b){uint32_t m=a+(b-a)/2;if(cmp_at(p,field,m,q,&c))return -1;if(c<0)a=m+1;else b=m;}
 *lo=a;b=library_count();
 while(a<b){uint32_t m=a+(b-a)/2;if(cmp_at(p,field,m,q,&c))return -1;if(c<=0)a=m+1;else b=m;}
 *hi=a;return 0;
}
/* ---- word search (v1.2) ----
 * Every word start of a title is one entry: offset 0, and each position
 * whose byte is not a separator but follows one (ASCII separators only;
 * the apostrophe is part of a word), at most WORD_MAX per title. Entries
 * are sorted by the folded title from the word on (WORD_KEY bytes, then
 * offset, dup, id). A query matches an entry when its folded bytes are a
 * prefix of the entry's key; the matches are one contiguous range.
 * dup = the longest common key prefix (bytes, max 255) with an earlier word
 * of the same title. For a query of qb folded bytes an entry is shown only
 * when dup < qb: an earlier word of the title also matches the query
 * exactly when it shares at least qb bytes with this one (all matches
 * share the query's qb bytes), so each song is shown once, at its first
 * matching word. */
static int word_sep(uint8_t c){return c==' '||c=='\t'||(c&&strchr("-_.,;:!?/\\()[]{}&+\"*#~|",c));}
static unsigned word_starts(const char *t,uint8_t *at){
 unsigned n=0;for(unsigned p=0;t[p]&&n<WORD_MAX;p++)if(!p||(!word_sep((uint8_t)t[p])&&word_sep((uint8_t)t[p-1])))at[n++]=(uint8_t)p;
 return n;
}
/* Entry key: rec_tmp after library_song() decoded it. Query key: rec (no
 * update runs while searching). */
#define wkey ((char*)rec_tmp)
#define qkey ((char*)rec)
unsigned library_words(void){return src_open?head.wrd_count:0;}
static int word_entry_key(uint32_t e){
 uint8_t b[4],p;if(src_read(head.wrd_id_off+4*e,b,4)||src_read(head.wrd_pos_off+e,&p,1))return -1;
 uint32_t id=le32(b);if(library_song(id,&probe_song)||p>=strlen(probe_song.title))return -1;
 fold_key(wkey,WORD_KEY,probe_song.title+p);return 0;
}
static int word_dup(uint32_t e,uint8_t *d){return src_read(head.wrd_dup_off+e,d,1);}
/* Entry e's key against the query's first qb bytes: the stored prefix
 * decides unless it equals the query's first WORD_PRE bytes. */
static int word_cmp(uint32_t e,unsigned qb,int *c){
 uint8_t pre[WORD_PRE];if(src_read(head.wrd_pre_off+(uint64_t)e*WORD_PRE,pre,WORD_PRE))return -1;
 unsigned k=qb<WORD_PRE?qb:WORD_PRE;*c=memcmp(pre,qkey,k);
 if(*c||qb<=WORD_PRE)return 0;
 if(word_entry_key(e))return -1;*c=memcmp(wkey,qkey,qb);return 0;
}
int library_word_range(const char *q,uint32_t *lo,uint32_t *hi,unsigned *qb,uint32_t *count){
 *lo=*hi=*count=0;*qb=(unsigned)fold_key(qkey,WORD_KEY,q);
 if(!library_words()||!*qb)return -1;
 uint32_t a=0,b=head.wrd_count;int c;
 while(a<b){uint32_t m=a+(b-a)/2;if(word_cmp(m,*qb,&c))return -1;if(c<0)a=m+1;else b=m;}
 *lo=a;b=head.wrd_count;
 while(a<b){uint32_t m=a+(b-a)/2;if(word_cmp(m,*qb,&c))return -1;if(c<=0)a=m+1;else b=m;}
 *hi=a;
 uint8_t d[64];uint32_t n=0;
 for(uint32_t e=*lo;e<*hi;){unsigned k=*hi-e<64?*hi-e:64;if(src_read(head.wrd_dup_off+e,d,k))return -1;
  for(unsigned i=0;i<k;i++)n+=d[i]<*qb;e+=k;}
 *count=n;return 0;
}
int library_word_song(uint32_t lo,uint32_t hi,unsigned qb,uint32_t i,uint32_t *id){
 uint8_t d;
 if(!library_words()||hi>head.wrd_count||lo>=hi)return -1;
 if(!wmemo.valid||wmemo.lo!=lo||wmemo.hi!=hi||wmemo.qb!=qb||wmemo.gen!=head.generation){
  uint32_t e=lo;for(;;e++){if(e>=hi)return -1;if(word_dup(e,&d))return -1;if(d<qb)break;}
  wmemo.lo=lo;wmemo.hi=hi;wmemo.qb=qb;wmemo.gen=head.generation;wmemo.i=0;wmemo.e=e;wmemo.valid=1;
 }
 while(wmemo.i<i){uint32_t e=wmemo.e+1;for(;;e++){if(e>=hi)return -1;if(word_dup(e,&d))return -1;if(d<qb)break;}wmemo.e=e;wmemo.i++;}
 while(wmemo.i>i){uint32_t e=wmemo.e;for(;;){if(e<=lo)return -1;e--;if(word_dup(e,&d))return -1;if(d<qb)break;}wmemo.e=e;wmemo.i--;}
 uint8_t b[4];if(src_read(head.wrd_id_off+4*wmemo.e,b,4))return -1;
 *id=le32(b);return *id<head.count?0:-1;
}
static uint32_t path_hash(const char *rel){return fnv(2166136261u,rel,strlen(rel));}
int library_find(const char *path,uint32_t *id){
 if(!src_open||memcmp(path,"0:/gbamp3/",10))return -1;
 const char *rel=path+10;uint32_t h=path_hash(rel),a=0,b=head.count;
 while(a<b){uint32_t m=a+(b-a)/2,x;if(library_perm(PERM_PATH,m,&x)||library_song(x,&probe_song))return -1;
  uint32_t v=path_hash(probe_song.path+10);if(v<h)a=m+1;else b=m;}
 for(;a<head.count;a++){uint32_t x;if(library_perm(PERM_PATH,a,&x)||library_song(x,&probe_song))return -1;
  if(path_hash(probe_song.path+10)!=h)break;if(!strcmp(probe_song.path,path)){*id=x;return 0;}}
 return -1;
}

/* ---- update ---- */
static struct Header next;
static LibraryProgress report;
static int wr(FIL *f,uint32_t at,const void *p,unsigned n){UINT got=0;return f_lseek(f,at)==FR_OK&&f_write(f,p,n,&got)==FR_OK&&got==n?0:-1;}
static int rd(FIL *f,uint32_t at,void *p,unsigned n){UINT got=0;return f_lseek(f,at)==FR_OK&&f_read(f,p,n,&got)==FR_OK&&got==n?0:-1;}
static int appendf(FIL *f,const void *p,unsigned n){UINT got=0;return f_write(f,p,n,&got)==FR_OK&&got==n?0:-1;}
static const char *temp_name(unsigned k){
 static const char *const n[]={LIBRARY_DIR "/TMPO.DAT",LIBRARY_DIR "/TMPR.DAT",LIBRARY_DIR "/TMPG0.DAT",LIBRARY_DIR "/TMPG1.DAT",LIBRARY_DIR "/TMPG2.DAT",LIBRARY_DIR "/TMPG3.DAT"};
 return n[k];
}
int library_system_dir(void){
 static FILINFO f;FRESULT r=f_stat(LIBRARY_DIR,&f);
 if(r==FR_INT_ERR&&library_repair_dir(LIBRARY_DIR)>0)r=f_stat(LIBRARY_DIR,&f);   /* exFAT: a torn entry set */
 if(r!=FR_OK)return f_mkdir(LIBRARY_DIR)==FR_OK?0:-1;
 if(!(f.fattrib&AM_DIR))return -1;
 /* Visible on a PC: up to v2.2 the folder was made hidden + system. Cleared
  * at the first call (at start); a failure (write-protected card) is ignored. */
 if(f.fattrib&(AM_HID|AM_SYS))f_chmod(LIBRARY_DIR,0,AM_HID|AM_SYS);
 return 0;
}
/* v2.2 exFAT: a power cut (or a failed write) between the sector writes of
 * one entry set leaves the set torn and FatFs fails every lookup in that
 * folder with FR_INT_ERR. exfat_repair_dir keeps the last complete state
 * (exfat_repair.h); on FAT it does nothing. `dir` and the folders above it
 * (only `dir` when its path is too long for that). Writes must be allowed. */
int library_repair_dir(const char *dir){
 return exfat_repair_dir(storage_fs(),dir,strlen(dir)<EXFAT_REPAIR_PATH_MAX);
}
/* The folder holding `path`. */
int library_repair_folder_of(const char *path){
 static char d[STORAGE_PATH];unsigned n=0,cut=0;
 for(;path[n];n++){if(n+1>=STORAGE_PATH)return -1;d[n]=path[n];if(path[n]=='/')cut=n;}
 if(!cut)return -1;
 d[d[cut-1]==':'?cut+1:cut]=0;   /* "0:/a.mp3" -> "0:/" */
 return library_repair_dir(d);
}
/* f_open; on FR_INT_ERR the file's folder is repaired and it is tried once more. */
FRESULT library_open(FIL *f,const char *path,BYTE mode){
 FRESULT r=f_open(f,path,mode);
 if(r==FR_INT_ERR&&library_repair_folder_of(path)>0)r=f_open(f,path,mode);
 return r;
}
/* Walk state */
static DIR dirs[16];
static char walk[STORAGE_PATH];
static unsigned lens[16];
static uint32_t old_cursor;
static struct SongMeta meta;
/* One record buffer for the walk (old_song), the sort (ks) and the covers:
 * the phases never overlap (EWRAM is tight). */
static struct LibSong old_song;
#define ks old_song
/* Previous record for the same path (walk orders match), else -1. */
static int reuse(const char *rel,uint32_t size,uint32_t date,unsigned *len){
 if(!src_open)return -1;
 for(uint32_t k=old_cursor;k<head.count&&k<old_cursor+16;k++){
  uint32_t pos;unsigned n;if(record_pos(k,&pos)||read_record_at(pos,rec,&n)||decode(rec,n,&old_song))continue;
  if(strcmp(old_song.path+10,rel))continue;
  old_cursor=k+1;
  if(old_song.size!=size||old_song.date!=date)return -1;
  *len=n;return 0;
 }
 return -1;
}
static FIL probe;
/* -1: unreadable song (skipped), -2: cancelled, -3: index write failed. */
static int add_song(const char *full,const DIR *parent,const FILINFO *fi){
 const char *rel=full+10;uint32_t date=(uint32_t)fi->fdate<<16|fi->ftime;unsigned len;
 if(reuse(rel,(uint32_t)fi->fsize,date,&len)){
  if(f_open_entry(&probe,parent,fi)!=FR_OK)return -1;   /* no second path lookup */
  int e=meta_read(&probe,fi->fname,&meta);f_close(&probe);if(e)return -1;
  len=encode(rec,rel,&meta,(uint32_t)fi->fsize,date);
 }
 uint8_t o[4];put32(o,next.rec_len);
 if(wr(&dst,next.rec_off+next.rec_len,rec,len)||appendf(&tmp_off,o,4))return -3;
 next.rec_len+=len;next.count++;
 if(report&&!(next.count&7)&&report(0,next.count))return -2;
 folder_yield();return 0;
}
static FILINFO fi;
static char child[STORAGE_PATH],full[STORAGE_PATH];
static int walk_tree(void){
 unsigned d=0;memcpy(walk,"0:/gbamp3",10);lens[0]=9;
 if(f_opendir(&dirs[0],walk)!=FR_OK)return -1;
 for(;;){
  if(f_readdir(&dirs[d],&fi)!=FR_OK)return -1;
  if(!fi.fname[0]){f_closedir(&dirs[d]);if(!d)return 0;d--;walk[lens[d]]=0;continue;}
  if(fi.fname[0]=='.'||(fi.fattrib&(AM_HID|AM_SYS)))continue;
  if(fi.fattrib&AM_DIR){
   if(folder_system(fi.fname)||d+1>=16)continue;
   if(!folder_join(child,walk,fi.fname))continue;
   memcpy(walk,child,strlen(child)+1);
   if(f_opendir_entry(&dirs[d+1],&dirs[d],&fi)!=FR_OK){walk[lens[d]]=0;continue;}
   d++;lens[d]=strlen(walk);continue;
  }
  if(!folder_is_song(fi.fname))continue;
  if(!folder_join(full,walk,fi.fname))continue;
  int e=add_song(full,&dirs[d],&fi);if(e==-2)return -2;if(e==-3)return -1;
  /* A file that cannot be read is left out rather than failing the update. */
 }
}
/* Sort keys: fixed-width folded fields, 0xFF-filled when empty (sort last). */
static void kfield(uint8_t *k,unsigned width,const char *s){
 if(!s[0]){memset(k,0xff,width);return;}
 fold_key((char*)k,width,s);
}
static void knum(uint8_t *k,unsigned v){k[0]=(uint8_t)(v>>8);k[1]=(uint8_t)v;}
static void make_key(unsigned p,const struct LibSong *s,uint8_t *k){
 memset(k,0,KEY_BYTES);
 switch(p){
  case PERM_TITLE:kfield(k,64,s->title);kfield(k+64,32,s->artist);kfield(k+96,28,s->album);break;
  case PERM_ARTIST:kfield(k,40,s->artist);kfield(k+40,40,s->album);knum(k+80,s->disc?s->disc:1);knum(k+82,s->track?s->track:0xffff);kfield(k+84,40,s->title);break;
  case PERM_ALBUM:kfield(k,40,s->album);kfield(k+40,40,s->album_artist);knum(k+80,s->disc?s->disc:1);knum(k+82,s->track?s->track:0xffff);kfield(k+84,40,s->title);break;
  case PERM_GENRE:kfield(k,40,s->genre);kfield(k+40,60,s->title);kfield(k+100,24,s->artist);break;
  case PERM_FILE:kfield(k,KEY_BYTES,basename(s->path));break;
  default:{uint32_t h=path_hash(s->path+10);k[0]=(uint8_t)(h>>24);k[1]=(uint8_t)(h>>16);k[2]=(uint8_t)(h>>8);k[3]=(uint8_t)h;
   unsigned n=strlen(s->path+10);if(n>KEY_BYTES-4)n=KEY_BYTES-4;memcpy(k+4,s->path+10,n);}
 }
}
static int keycmp(const uint8_t *a,const uint8_t *b){return memcmp(a,b,SORT_REC);} /* id (big endian) breaks ties */
static void set_id(uint8_t *r,uint32_t id){r[124]=(uint8_t)(id>>24);r[125]=(uint8_t)(id>>16);r[126]=(uint8_t)(id>>8);r[127]=(uint8_t)id;}
static uint32_t get_id(const uint8_t *r){return (uint32_t)r[124]<<24|(uint32_t)r[125]<<16|(uint32_t)r[126]<<8|r[127];}
static uint16_t order[SORT_CAP];
static void sort_chunk(unsigned n){
 /* Heapsort of indexes by record bytes. */
 for(unsigned i=0;i<n;i++)order[i]=(uint16_t)i;
 #define REC(i) (sortbuf+(unsigned)order[i]*SORT_REC)
 for(unsigned start=n/2;start-->0;){unsigned r=start;for(;;){unsigned c=2*r+1;if(c>=n)break;if(c+1<n&&keycmp(REC(c),REC(c+1))<0)c++;if(keycmp(REC(r),REC(c))>=0)break;uint16_t t=order[r];order[r]=order[c];order[c]=t;r=c;}}
 for(unsigned end=n;end-->1;){uint16_t t=order[0];order[0]=order[end];order[end]=t;unsigned r=0;
  for(;;){unsigned c=2*r+1;if(c>=end)break;if(c+1<end&&keycmp(REC(c),REC(c+1))<0)c++;if(keycmp(REC(r),REC(c))>=0)break;t=order[r];order[r]=order[c];order[c]=t;r=c;}}
 #undef REC
}
/* Output stage shared by the direct and merged paths. */
static uint8_t prev_key[SORT_REC];
static uint32_t out_i,out_buf[128],out_n,g_start[3],g_rep[3],g_count[GROUP_COUNT],aa_first;
#define out_pos rec          /* word pass output: rec is idle once records are generated */
#define out_dup (rec+64)
#define out_pre (rec+128)    /* 64 x WORD_PRE bytes */
static unsigned have_prev;
static int flush_out(unsigned p){
 if(!out_n)return 0;static uint8_t b[512];for(unsigned i=0;i<out_n;i++)put32(b+4*i,out_buf[i]);
 uint32_t at=out_i-out_n;int e;
 if(p==WORD_PASS)e=wr(&dst,next.wrd_id_off+4*at,b,4*out_n)||wr(&dst,next.wrd_pos_off+at,out_pos,out_n)||wr(&dst,next.wrd_dup_off+at,out_dup,out_n)
                 ||wr(&dst,next.wrd_pre_off+at*WORD_PRE,out_pre,out_n*WORD_PRE);
 else e=wr(&dst,next.perm_off[p]+4*at,b,4*out_n);
 out_n=0;return e;
}
static int emit_group(unsigned kind,uint32_t start,uint32_t end,uint32_t rep,uint32_t sub_start,uint32_t sub_count){
 uint8_t g[GROUP_BYTES];put32(g,start);put32(g+4,end-start);put32(g+8,rep);put32(g+12,sub_start);put32(g+16,sub_count);
 g_count[kind]++;return appendf(&grp_tmp[kind],g,GROUP_BYTES);
}
/* Group boundaries by key prefix: level widths per permutation. */
static int close_groups(unsigned p,unsigned level_from){
 if(p==PERM_ARTIST){
  if(level_from<=1&&emit_group(GROUP_ARTIST_ALBUMS,g_start[1],out_i,g_rep[1],0,0))return -1;
  if(level_from==0&&emit_group(GROUP_ARTISTS,g_start[0],out_i,g_rep[0],aa_first,g_count[GROUP_ARTIST_ALBUMS]-aa_first))return -1;
 }else if(p==PERM_ALBUM){if(emit_group(GROUP_ALBUMS,g_start[0],out_i,g_rep[0],0,0))return -1;}
 else if(p==PERM_GENRE){if(emit_group(GROUP_GENRES,g_start[0],out_i,g_rep[0],0,0))return -1;}
 return 0;
}
static int emit(unsigned p,const uint8_t *r){
 uint32_t id=get_id(r);
 if(p==PERM_ARTIST||p==PERM_ALBUM||p==PERM_GENRE){
  /* Albums group by name alone: compilations without an album-artist tag stay
   * one album; explicit album artists only order songs inside the group. */
  unsigned w0=40;
  int new0=!have_prev||memcmp(prev_key,r,w0),new1=p==PERM_ARTIST&&(new0||memcmp(prev_key,r,80));
  if(have_prev&&(new0||new1)&&close_groups(p,new0?0:1))return -1;
  if(new0){g_start[0]=out_i;g_rep[0]=id;if(p==PERM_ARTIST)aa_first=g_count[GROUP_ARTIST_ALBUMS];}
  if(new1){g_start[1]=out_i;g_rep[1]=id;}
 }
 memcpy(prev_key,r,SORT_REC);have_prev=1;
 if(p==WORD_PASS){out_pos[out_n]=r[WORD_KEY];out_dup[out_n]=r[WORD_KEY+1];memcpy(out_pre+out_n*WORD_PRE,r,WORD_PRE);}
 out_buf[out_n++]=id;out_i++;
 if(out_n==(p==WORD_PASS?64u:128u))return flush_out(p);
 return 0;
}
static int finish(unsigned p){
 if(have_prev&&(p==PERM_ARTIST||p==PERM_ALBUM||p==PERM_GENRE)&&close_groups(p,0))return -1;
 return flush_out(p);
}
/* Read back records sequentially from the new slot. */
static uint32_t scan_pos;
static int next_song(struct LibSong *s){
 uint8_t *r=rec;
 if(rd(&dst,next.rec_off+scan_pos,r,32))return -1;
 unsigned n=(unsigned)r[0]|(unsigned)r[1]<<8;if(n<REC_HEAD||n>REC_MAX||rd(&dst,next.rec_off+scan_pos+32,r+32,n-32))return -1;
 scan_pos+=n;return decode(r,n,s);
}
/* v1.2: the word entries of one title (section "word search"): key =
 * folded title from the word (WORD_KEY bytes), then offset, dup, id. */
#define lcp_a ((char*)rec)   /* next_song() is done with rec for this title */
static unsigned word_records(const struct LibSong *s,uint32_t id,unsigned slot0,unsigned room,unsigned first){
 uint8_t at[WORD_MAX];unsigned n=word_starts(s->title,at),made=0;
 for(unsigned k=first;k<n&&made<room;k++,made++){
  uint8_t *r=sortbuf+(slot0+made)*SORT_REC;memset(r,0,SORT_REC);
  fold_key((char*)r,WORD_KEY,s->title+at[k]);
  unsigned dup=0;
  for(unsigned j=0;j<k;j++){
   fold_key(lcp_a,WORD_KEY,s->title+at[j]);
   unsigned l=0;while(l<WORD_KEY&&lcp_a[l]==(char)r[l])l++;if(l>dup)dup=l;
  }
  r[WORD_KEY]=at[k];r[WORD_KEY+1]=(uint8_t)(dup>255?255:dup);set_id(r,id);
 }
 return made;
}
static unsigned word_total(const struct LibSong *s){uint8_t at[WORD_MAX];return word_starts(s->title,at);}
/* Sort one pass: permutation p (one record per song) or WORD_PASS (one per
 * title word). Chunks of SORT_CAP records become runs on the card; all runs
 * but the last are full. Returns 1 when the word index would exceed
 * WORD_ENTRY_CAP (it is then left out; nothing was written to the slot). */
static uint32_t pass_total;
static void words_layout(uint32_t n){
 next.wrd_count=n;next.wrd_pos_off=next.wrd_id_off+n*4;next.wrd_dup_off=next.wrd_pos_off+n;next.wrd_pre_off=next.wrd_dup_off+n;
}
static int build_perm(unsigned p){
 out_i=out_n=0;have_prev=0;
 unsigned runs=0,fill=0,phase=p==WORD_PASS?8:1+p;scan_pos=0;pass_total=0;
 f_close(&run_a);
 if(f_open(&run_a,temp_name(1),FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 for(uint32_t id=0;id<next.count;id++){
  if(next_song(&ks))return -1;
  unsigned want=p==WORD_PASS?word_total(&ks):1,done=0;
  if(p==WORD_PASS&&pass_total+want>WORD_ENTRY_CAP)return 1;
  while(done<want){
   if(fill==SORT_CAP){   /* flush a full chunk only when more records follow */
    sort_chunk(fill);
    for(unsigned i=0;i<fill;i++)if(wr(&run_a,(runs*SORT_CAP+i)*SORT_REC,sortbuf+(unsigned)order[i]*SORT_REC,SORT_REC))return -1;
    runs++;fill=0;folder_yield();
    if(report&&report(phase,id+1))return -2;
   }
   unsigned k;
   if(p==WORD_PASS)k=word_records(&ks,id,fill,SORT_CAP-fill,done);
   else{uint8_t *r=sortbuf+fill*SORT_REC;make_key(p,&ks,r);set_id(r,id);k=1;}
   fill+=k;done+=k;pass_total+=k;
  }
 }
 if(p==WORD_PASS)words_layout(pass_total);
 if(!pass_total)return finish(p);
 if(!runs){sort_chunk(fill);for(unsigned i=0;i<fill;i++)if(emit(p,sortbuf+(unsigned)order[i]*SORT_REC))return -1;return finish(p);}
 sort_chunk(fill);
 for(unsigned i=0;i<fill;i++)if(wr(&run_a,(runs*SORT_CAP+i)*SORT_REC,sortbuf+(unsigned)order[i]*SORT_REC,SORT_REC))return -1;
 runs++;folder_yield();
 /* k-way merge; each run gets an equal share of the sort buffer. */
 if(runs>SORT_CAP)return -1;
 unsigned per=SORT_CAP/runs;
 /* Counts stay <= SORT_CAP, so 16 bits suffice (EWRAM is tight). Run heads
  * sit in a binary min-heap: O(log runs) comparisons per record. */
 static uint16_t taken[SORT_CAP],have[SORT_CAP],head_i[SORT_CAP],heap[SORT_CAP];
 uint32_t last=pass_total-(runs-1)*SORT_CAP;
 #define HEAD(r) (sortbuf+((unsigned)(r)*per+head_i[r])*SORT_REC)
 unsigned hn=0;
 for(unsigned r=0;r<runs;r++){
  taken[r]=have[r]=head_i[r]=0;
  unsigned total=r+1==runs?last:SORT_CAP,k=total<per?total:per;
  if(rd(&run_a,(uint32_t)r*SORT_CAP*SORT_REC,sortbuf+r*per*SORT_REC,k*SORT_REC))return -1;
  taken[r]=(uint16_t)k;have[r]=(uint16_t)k;
  unsigned i=hn++;heap[i]=(uint16_t)r;   /* sift up */
  while(i&&keycmp(HEAD(heap[i]),HEAD(heap[(i-1)/2]))<0){uint16_t t=heap[i];heap[i]=heap[(i-1)/2];heap[(i-1)/2]=t;i=(i-1)/2;}
 }
 for(uint32_t done=0;done<pass_total;done++){
  if(!hn)return -1;
  unsigned best=heap[0];
  if(emit(p,HEAD(best)))return -1;
  head_i[best]++;
  if(head_i[best]==have[best]){
   unsigned total=best+1==runs?last:SORT_CAP;
   if(taken[best]==total){heap[0]=heap[--hn];}      /* run exhausted */
   else{unsigned k=total-taken[best]<per?total-taken[best]:per;
    if(rd(&run_a,((uint32_t)best*SORT_CAP+taken[best])*SORT_REC,sortbuf+best*per*SORT_REC,k*SORT_REC))return -1;
    taken[best]=(uint16_t)(taken[best]+k);have[best]=(uint16_t)k;head_i[best]=0;}
  }
  for(unsigned i=0;;){   /* sift down */
   unsigned l=2*i+1,m=i;
   if(l<hn&&keycmp(HEAD(heap[l]),HEAD(heap[m]))<0)m=l;
   if(l+1<hn&&keycmp(HEAD(heap[l+1]),HEAD(heap[m]))<0)m=l+1;
   if(m==i)break;uint16_t t=heap[i];heap[i]=heap[m];heap[m]=t;i=m;
  }
  if(!(done&255)){folder_yield();if(report&&report(phase,done))return -2;}
 }
 #undef HEAD
 return finish(p);
}
static int copy_file(FIL *from,uint32_t bytes,uint32_t at){
 uint32_t done=0;
 while(done<bytes){unsigned k=bytes-done<sizeof(sortbuf)?bytes-done:sizeof(sortbuf);
  if(rd(from,done,sortbuf,k)||wr(&dst,at+done,sortbuf,k))return -1;done+=k;folder_yield();}
 return 0;
}
/* ---- album covers (v0.9.8) ---- */
#ifdef __arm__
static unsigned clock_now(void){unsigned h,l,h2;do{h=*(volatile uint16_t*)0x400010c;l=*(volatile uint16_t*)0x4000108;h2=*(volatile uint16_t*)0x400010c;}while(h!=h2);return h<<16|l;}
#else
static unsigned clock_now(void){return 0;}
#endif
#define COVER_CYCLES (JPEG_MAX_SECONDS*16777216u)
static unsigned cover_started;
unsigned library_cover_decodes;
static uint32_t pic_base;
static int pic_read(void *ctx,uint32_t at,uint8_t *buf,unsigned cap){
 (void)ctx;UINT got=0;
 /* End on a sector boundary of the file: FatFS then reads whole sectors
  * straight into the buffer instead of through its window. */
 uint32_t end=(pic_base+at+cap)&~511u;
 if(end>pic_base+at)cap=end-(pic_base+at);
 if(f_lseek(&probe,pic_base+at)!=FR_OK||f_read(&probe,buf,cap,&got)!=FR_OK)return -1;
 return (int)got;
}
static int pic_stop(void *ctx){(void)ctx;folder_yield();return clock_now()-cover_started>COVER_CYCLES;}
static int song_at(uint32_t pos,uint32_t *id,struct LibSong *s){
 uint8_t b[4];if(rd(&dst,next.perm_off[PERM_ALBUM]+4*pos,b,4))return -1;*id=le32(b);
 if(*id>=next.count||rd(&dst,next.off_off+4**id,b,4))return -1;
 uint32_t at=next.rec_off+le32(b);uint8_t *r=rec;
 if(rd(&dst,at,r,32))return -1;unsigned n=(unsigned)r[0]|(unsigned)r[1]<<8;
 if(n<REC_HEAD||n>REC_MAX||rd(&dst,at+32,r+32,n-32))return -1;
 return decode(r,n,s);
}
/* A GAC's own RGB555 cover (64x64, or 32x32 doubled), checked against the
 * header's hash. */
static int gac_cover(const struct LibSong *s,uint16_t *img){
 unsigned char h[64];struct GacInfo info;UINT got=0;int e=-1;
 if(f_open(&probe,s->path,FA_READ)!=FR_OK)return -1;
 if(f_read(&probe,h,64,&got)==FR_OK&&got==64&&gac_header(h,64,(unsigned)f_size(&probe),&info)==1&&info.art
    &&(info.art_bytes==COVER_BYTES_64||info.art_bytes==COVER_BYTES_32)&&f_lseek(&probe,info.art)==FR_OK
    &&f_read(&probe,img,info.art_bytes,&got)==FR_OK&&got==info.art_bytes&&gac_cover_valid(h,(const unsigned char*)img)){
  if(info.art_bytes==COVER_BYTES_32)cover_double(img);
  e=0;
 }
 f_close(&probe);return e;
}
/* A song's reduced cover record: the previous index's record when it was
 * made from this very file, unchanged (size, date, picture position), else
 * the picture decoded (MP3) or read (GAC) now and reduced. img: 8 KB; work:
 * JPEG_WORK_BYTES (the reduction's box table after the decode);
 * out: COVER_REC bytes. */
static int song_cover(const struct LibSong *s,uint16_t *img,uint8_t *work,uint8_t *out){
 uint32_t old;uint8_t b[4],p[4];
 if(!library_find(s->path,&old)&&probe_song.size==s->size&&probe_song.date==s->date&&probe_song.pic_off==s->pic_off&&probe_song.pic_len==s->pic_len
    &&!src_read(head.cov_map_off+4*old,b,4)&&le32(b)<head.cov_count&&!src_read(head.cov_src_off+4*le32(b),p,4)&&le32(p)==old
    &&!src_read(head.cov_img_off+(uint64_t)le32(b)*COVER_BYTES,out,COVER_BYTES))return 0;
 int e=-1;library_cover_decodes++;
 if(s->format==FORMAT_GAC)e=gac_cover(s,img);
 else{
  if(f_open(&probe,s->path,FA_READ)!=FR_OK)return -1;
  if(s->pic_off<f_size(&probe)&&s->pic_len<=f_size(&probe)-s->pic_off){
   pic_base=s->pic_off;cover_started=clock_now();
   e=jpeg_dc_cover(pic_read,pic_stop,0,s->pic_len,work,img);
  }
  f_close(&probe);
 }
 if(e)return -1;
 jpeg_cover_reduce(img,out,work);
 return 0;
}
/* One cover per album group (never "Unknown Album"): the first song in album
 * order (MP3 with a picture, or GAC with a cover) whose picture is usable.
 * Then the per-song map, in chunks of ids. */
static int album_covers(void){
 uint16_t *img=(uint16_t*)(sortbuf+JPEG_WORK_BYTES);uint8_t *out=sortbuf+8*COVER_PIXELS;   /* after the reduction's work */
 uint32_t groups=g_count[GROUP_ALBUMS],*map=(uint32_t*)sortbuf;   /* map: sortbuf[0, 32 KB) */
 f_close(&run_a);
 if(f_open(&run_a,temp_name(1),FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 for(uint32_t g=0;g<groups;g++){
  uint8_t e[GROUP_BYTES];uint32_t cov=NO_COVER,id,src=NO_COVER;
  if(rd(&dst,next.grp_off[GROUP_ALBUMS]+GROUP_BYTES*g,e,GROUP_BYTES))return -1;
  uint32_t start=le32(e),n=le32(e+4);
  if(song_at(start,&id,&ks))return -1;
  if(ks.album[0])for(uint32_t j=start;j<start+n;j++){
   if(j!=start&&song_at(j,&id,&ks))return -1;
   if(!(ks.format==FORMAT_MP3&&ks.pic_len)&&!(ks.format==FORMAT_GAC&&ks.has_art))continue;
   if(song_cover(&ks,img,sortbuf,out))continue;
   if(wr(&dst,next.cov_img_off+next.cov_count*COVER_BYTES,out,COVER_BYTES))return -1;
   cov=next.cov_count++;src=id;break;
  }
  uint8_t o[8];put32(o,cov);put32(o+4,src);if(appendf(&run_a,o,8))return -1;
  folder_yield();if(report&&report(7,g+1))return -2;
 }
 if(f_sync(&run_a)!=FR_OK)return -1;
 const uint32_t chunk=32768/4;
 for(uint32_t base=0;base<next.count;base+=chunk){
  uint32_t n=next.count-base<chunk?next.count-base:chunk;
  memset(map,0xff,n*4);
  for(uint32_t g=0;g<groups;g++){
   uint8_t e[GROUP_BYTES],o[4];
   if(rd(&run_a,8*g,o,4))return -1;uint32_t cov=le32(o);if(cov==NO_COVER)continue;
   if(rd(&dst,next.grp_off[GROUP_ALBUMS]+GROUP_BYTES*g,e,GROUP_BYTES))return -1;
   uint32_t start=le32(e),cnt=le32(e+4);
   for(uint32_t j=0;j<cnt;j+=128){
    uint32_t k=cnt-j<128?cnt-j:128;uint8_t *ids=sortbuf+32768;
    if(rd(&dst,next.perm_off[PERM_ALBUM]+4*(start+j),ids,4*k))return -1;
    for(uint32_t i=0;i<k;i++){uint32_t id=le32(ids+4*i);if(id>=base&&id-base<n)map[id-base]=cov;}
   }
  }
  for(uint32_t i=0;i<n;i++){uint32_t v=map[i];put32((uint8_t*)&map[i],v);}
  if(wr(&dst,next.cov_map_off+4*base,map,4*n))return -1;
  folder_yield();
 }
 /* Providers, in cover order (covers were numbered in group order). */
 next.cov_src_off=next.cov_img_off+next.cov_count*COVER_BYTES;
 for(uint32_t g=0,k=0;g<groups;g++){
  uint8_t o[8];if(rd(&run_a,8*g,o,8))return -1;
  if(le32(o)!=NO_COVER&&wr(&dst,next.cov_src_off+4*k++,o+4,4))return -1;
 }
 return 0;
}
static void close_all(void){
 f_close(&dst);f_close(&tmp_off);f_close(&run_a);for(unsigned g=0;g<GROUP_COUNT;g++)f_close(&grp_tmp[g]);
}
static int update(void){
 unsigned slot=src_open?active^1:0;
 if(library_system_dir())return -1;
 if(library_open(&dst,slot_name[slot],FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 memset(sortbuf,0,HEAD_BYTES);if(wr(&dst,0,sortbuf,HEAD_BYTES)||f_sync(&dst)!=FR_OK)return -1;
 if(f_open(&tmp_off,temp_name(0),FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 for(unsigned g=0;g<GROUP_COUNT;g++)if(f_open(&grp_tmp[g],temp_name(2+g),FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 memset(&next,0,sizeof(next));memcpy(next.magic,"GBALIB7",8);next.generation=head.generation+1;if(!next.generation)next.generation=1;
 next.rec_off=HEAD_BYTES;old_cursor=0;
 int e=walk_tree();if(e)return e;
 next.off_off=(next.rec_off+next.rec_len+3)&~3u;
 for(unsigned p=0;p<PERM_COUNT;p++)next.perm_off[p]=next.off_off+next.count*4*(p+1);
 if(f_sync(&tmp_off)!=FR_OK||copy_file(&tmp_off,next.count*4,next.off_off))return -1;
 memset(g_count,0,sizeof(g_count));
 for(unsigned p=0;p<PERM_COUNT;p++){if(!next.count)break;e=build_perm(p);if(e)return e;}
 uint32_t at=next.perm_off[PERM_COUNT-1]+next.count*4;
 for(unsigned g=0;g<GROUP_COUNT;g++){
  next.grp_off[g]=at;next.grp_count[g]=g_count[g];
  if(f_sync(&grp_tmp[g])!=FR_OK||copy_file(&grp_tmp[g],g_count[g]*GROUP_BYTES,at))return -1;
  at+=g_count[g]*GROUP_BYTES;
 }
 next.cov_map_off=at;next.cov_img_off=next.cov_src_off=at+next.count*4;
 if(next.count&&(e=album_covers()))return e;
 /* v1.2: title word index after the cover providers (skipped when it would
  * exceed the merge limit: Song Title search then matches title starts). */
 next.wrd_id_off=next.cov_src_off+next.cov_count*4;words_layout(0);
 if(next.count){e=build_perm(WORD_PASS);if(e==1)words_layout(0);else if(e)return e;}
 next.complete=1;
 if(f_sync(&dst)!=FR_OK)return -1;
 next.checksum=fnv(2166136261u,&next,sizeof(next)-4);
 if(wr(&dst,0,&next,sizeof(next))||f_sync(&dst)!=FR_OK)return -1;
 close_all();
 struct Header check;if(!read_header(slot,&check)||memcmp(&check,&next,sizeof(next)))return -1;
 return 0;
}
uint8_t *library_scratch(unsigned *bytes){*bytes=SORT_BYTES;return sortbuf;}
int library_update(LibraryProgress progress){
 report=progress;cache_reset();
 folder_sort_busy=1;   /* the sort buffer doubles as folder-order scratch */
 disk_index_writes(1);
 int e=update();
 folder_sort_busy=0;folder_sort_epoch++;
 close_all();
 for(unsigned t=0;t<6;t++)f_unlink(temp_name(t));   /* TMPR.DAT also held the album covers */
 disk_index_writes(0);
 library_init();
 return e;
}
