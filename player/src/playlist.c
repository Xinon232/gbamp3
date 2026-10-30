/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). On-device playlists (no count limits). */
#include "playlist.h"
#include "library.h"
#include "text.h"
#include "ff.h"
#include <string.h>
extern void disk_index_writes(unsigned);
extern void (*folder_yield)(void);
struct DirHead {char magic[8];uint32_t generation,count,next_id,checksum;};
struct DirEntry {uint32_t id;char name[PLAYLIST_NAME];uint32_t checksum;};
static const char *const slots[2]={LIBRARY_DIR "/PLIST0.DAT",LIBRARY_DIR "/PLIST1.DAT"};
static struct DirHead head;
static unsigned active,valid;
static FIL df,pf,out;
/* playlist_entry has its own handle and sector: the audio queue reads entries
 * from inside folder_yield() while remove/rewrite keep `pf` and `sec` busy. */
static FIL ef;static uint8_t esec[512];
/* v1.6: the directory slot and one playlist file stay open between reads:
 * opening a file searches its folders on the card each time, and the
 * playlist screens read one row after another. Every change closes them
 * before and after (their sector buffers would be stale). */
static int df_slot=-1;static unsigned ef_open;static uint32_t ef_id,ef_count;
static void close_reads(void){if(df_slot>=0){f_close(&df);df_slot=-1;}if(ef_open){f_close(&ef);ef_open=0;}}
static uint32_t fnv(const void *p,unsigned n){const uint8_t *b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int at(FIL *f,uint32_t pos,void *p,unsigned n){UINT g=0;return f_lseek(f,pos)==FR_OK&&f_read(f,p,n,&g)==FR_OK&&g==n?0:-1;}
static int put(FIL *f,uint32_t pos,const void *p,unsigned n){UINT g=0;return f_lseek(f,pos)==FR_OK&&f_write(f,p,n,&g)==FR_OK&&g==n?0:-1;}
static int read_head(unsigned slot,struct DirHead *h){
 if(f_open(&df,slots[slot],FA_READ)!=FR_OK)return 0;
 int ok=!at(&df,0,h,sizeof(*h))&&!memcmp(h->magic,"GBAPLD01",8)&&h->checksum==fnv(h,sizeof(*h)-4)&&f_size(&df)>=sizeof(*h)+(FSIZE_t)h->count*sizeof(struct DirEntry);
 f_close(&df);return ok;
}
int playlist_init(void){
 close_reads();struct DirHead a,b;int va=read_head(0,&a),vb=read_head(1,&b);
 memset(&head,0,sizeof(head));valid=0;
 if(!va&&!vb){head.next_id=1;return 0;}
 active=vb&&(!va||b.generation>a.generation);head=active?b:a;valid=1;return 0;
}
unsigned playlist_count(void){return valid?head.count:0;}
static struct DirEntry entry;
static int read_entry(unsigned i,struct DirEntry *e){
 if(!valid||i>=head.count)return -1;
 if(df_slot!=(int)active){if(df_slot>=0)f_close(&df);df_slot=-1;if(f_open(&df,slots[active],FA_READ)!=FR_OK)return -1;df_slot=(int)active;}
 int r=at(&df,sizeof(head)+i*sizeof(*e),e,sizeof(*e));if(r){f_close(&df);df_slot=-1;}
 if(r||e->checksum!=fnv(e,sizeof(*e)-4)){e->name[0]=0;return -1;}
 e->name[PLAYLIST_NAME-1]=0;return 0;
}
int playlist_info(unsigned i,uint32_t *id,char *name){
 if(read_entry(i,&entry)){if(name)strcpy(name,"?");return -1;}
 if(id)*id=entry.id;if(name)memcpy(name,entry.name,PLAYLIST_NAME);return 0;
}
int playlist_find_name(const char *name){
 for(unsigned i=0;i<playlist_count();i++)if(!read_entry(i,&entry)&&!fold_compare(entry.name,name,0))return (int)i;
 return -1;
}
/* Rewrite the directory into the other slot: skip `drop`, replace `edit`
 * with `with`, append `add`, swap `sa`/`sb`. Header written last. */
static int rewrite(int drop,int edit,const struct DirEntry *with,const struct DirEntry *add,int sa,int sb){
 if(df_slot==(int)(valid?active^1:0)){f_close(&df);df_slot=-1;}   /* the slot about to be rewritten */
 unsigned slot=valid?active^1:0;struct DirHead h=head;h.generation++;h.count=0;memcpy(h.magic,"GBAPLD01",8);
 if(library_system_dir())return -1;
 if(library_open(&out,slots[slot],FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return -1;
 static struct DirHead zero;int e=put(&out,0,&zero,sizeof(zero));
 unsigned n=playlist_count();
 for(unsigned i=0;i<n&&!e;i++){
  /* Move sa to position sb (the others shift up or down by one). */
  unsigned from=i;
  if(sa>=0&&sb>=0&&sa!=sb){int k=(int)i;
   if(k==sb)from=(unsigned)sa;
   else if(sa<sb&&k>=sa&&k<sb)from=i+1;
   else if(sa>sb&&k>sb&&k<=sa)from=i-1;}
  if((int)i==drop)continue;
  if((int)i==edit)entry=*with;else if(read_entry(from,&entry)){e=-1;break;}
  e=put(&out,sizeof(h)+h.count*sizeof(entry),&entry,sizeof(entry));h.count++;
  if(!(i&15))folder_yield();
 }
 if(!e&&add){e=put(&out,sizeof(h)+h.count*sizeof(*add),add,sizeof(*add));h.count++;}
 if(!e){e=f_sync(&out)!=FR_OK;h.checksum=fnv(&h,sizeof(h)-4);if(!e)e=put(&out,0,&h,sizeof(h))||f_sync(&out)!=FR_OK;}
 if(f_close(&out)!=FR_OK)e=-1;
 if(e)return -1;
 head=h;active=slot;valid=1;return 0;
}
static int clean_name(const char *name,struct DirEntry *e){
 while(*name==' ')name++;unsigned n=strlen(name);while(n&&name[n-1]==' ')n--;
 if(!n)return -3;
 /* Cut at 63 bytes without splitting a UTF-8 sequence. */
 if(n>=PLAYLIST_NAME){n=PLAYLIST_NAME-1;while(n&&((unsigned char)name[n]&0xc0)==0x80)n--;}
 memset(e,0,sizeof(*e));memcpy(e->name,name,n);return 0;
}
static char file_name[48];
/* Copy 0: Pxxxxxxxx.DAT, copy 1: Pxxxxxxxx.DA2 (v2.2, see "contents"). */
static const char *pfile_copy(uint32_t id,unsigned copy){
 static const char hex[]="0123456789ABCDEF";memcpy(file_name,LIBRARY_DIR "/P",sizeof(LIBRARY_DIR "/P")-1);
 char *p=file_name+sizeof(LIBRARY_DIR "/P")-1;for(int i=7;i>=0;i--)*p++=hex[(id>>(i*4))&15];memcpy(p,copy?".DA2":".DAT",5);return file_name;
}
static const char *pfile(uint32_t id){return pfile_copy(id,0);}
struct FileHead {char magic[8];uint32_t id,count,checksum;};
/* v2.2: the generation follows the v1 header in sector 0 (older versions
 * ignore it and still read the list); no generation = an old file, 1. */
struct FileGen {char magic[4];uint32_t generation,checksum;};
#define GEN_AT 64
static uint8_t other[512];   /* second sector buffer: swaps, the entry being added */
static int write_file_head(FIL *f,uint32_t id,uint32_t count,uint32_t gen){
 static uint8_t sec[512];memset(sec,0,512);struct FileHead h;memcpy(h.magic,"GBAPLE01",8);h.id=id;h.count=count;h.checksum=fnv(&h,sizeof(h)-4);
 memcpy(sec,&h,sizeof(h));struct FileGen g;memcpy(g.magic,"GEN1",4);g.generation=gen;memcpy(sec+GEN_AT,&g,8);g.checksum=fnv(sec,GEN_AT+8);
 memcpy(sec+GEN_AT,&g,sizeof(g));return put(f,0,sec,512)||f_sync(f)!=FR_OK;
}
/* Header of an open copy: 0 and its count/generation if valid. */
static int read_file_head(FIL *f,uint32_t id,uint32_t *count,uint32_t *gen){
 uint8_t b[GEN_AT+sizeof(struct FileGen)];struct FileHead h;struct FileGen g;
 if(at(f,0,b,sizeof(b)))return -1;memcpy(&h,b,sizeof(h));memcpy(&g,b+GEN_AT,sizeof(g));
 if(memcmp(h.magic,"GBAPLE01",8)||h.checksum!=fnv(&h,sizeof(h)-4)||h.id!=id)return -1;
 if(f_size(f)<(FSIZE_t)(1+h.count)*512)return -1;
 uint32_t c=g.checksum;memset(b+GEN_AT+8,0,4);
 *gen=!memcmp(g.magic,"GEN1",4)&&c==fnv(b,GEN_AT+8)&&g.generation?g.generation:1;
 *count=h.count;return 0;
}
static int playlist_create_do(const char *name,uint32_t *id){
 struct DirEntry e;int r=clean_name(name,&e);if(r)return r;
 if(playlist_find_name(e.name)>=0)return -2;
 disk_index_writes(1);
 e.id=head.next_id?head.next_id:1;head.next_id=e.id+1;e.checksum=fnv(&e,sizeof(e)-4);
 int err=library_system_dir();
 if(!err&&library_open(&pf,pfile(e.id),FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)err=-1;
 if(!err){err=write_file_head(&pf,e.id,0,1);if(f_close(&pf)!=FR_OK)err=-1;}
 if(!err){FRESULT u=f_unlink(pfile_copy(e.id,1));if(u!=FR_OK&&u!=FR_NO_FILE)err=-1;}   /* a second copy left by an earlier id */
 if(!err)err=rewrite(-1,-1,0,&e,-1,-1);
 disk_index_writes(0);
 if(!err&&id)*id=e.id;return err;
}
int playlist_create(const char *name,uint32_t *id){close_reads();int r=playlist_create_do(name,id);close_reads();return r;}
static int playlist_rename_do(unsigned i,const char *name){
 struct DirEntry e;int r=clean_name(name,&e);if(r)return r;
 int other=playlist_find_name(e.name);if(other>=0&&(unsigned)other!=i)return -2;
 if(read_entry(i,&entry))return -1;e.id=entry.id;e.checksum=fnv(&e,sizeof(e)-4);
 disk_index_writes(1);int err=rewrite(-1,(int)i,&e,0,-1,-1);disk_index_writes(0);return err;
}
int playlist_rename(unsigned i,const char *name){close_reads();int r=playlist_rename_do(i,name);close_reads();return r;}
static int playlist_delete_do(unsigned i){
 uint32_t id;if(playlist_info(i,&id,0))return -1;
 disk_index_writes(1);int err=rewrite((int)i,-1,0,0,-1,-1);if(!err){f_unlink(pfile_copy(id,0));f_unlink(pfile_copy(id,1));}disk_index_writes(0);return err;
}
int playlist_delete(unsigned i){close_reads();int r=playlist_delete_do(i);close_reads();return r;}
static int playlist_move_do(unsigned a,unsigned b){
 if(a>=playlist_count()||b>=playlist_count()||a==b)return -1;
 disk_index_writes(1);int err=rewrite(-1,-1,0,0,(int)a,(int)b);disk_index_writes(0);return err;
}
int playlist_move(unsigned a,unsigned b){close_reads();int r=playlist_move_do(a,b);close_reads();return r;}
/* ---- contents ----
 * v2.2: two copies per playlist (pfile_copy). A change is made to one copy at
 * a time: its header is cleared first (the copy is invalid), then its entries
 * change, then the header is written last with the next generation. The
 * second copy (.DA2) is changed first, the main copy (.DAT) last. Readers use
 * the main copy and the second one only while the main one is invalid, so
 * after a power cut they see the old list (cut before the main copy was
 * touched) or the new one (cut while it was changed), never a mix; reading
 * costs one file open as before. An old single copy is copied once first. */
static int open_copy(FIL *f,uint32_t id,unsigned c,BYTE mode,uint32_t *count,uint32_t *gen){
 if(f_open(f,pfile_copy(id,c),mode)!=FR_OK)return -1;
 if(read_file_head(f,id,count,gen)){f_close(f);return -1;}
 return 0;
}
/* Open (in `mode`) the copy readers use: 0 = main, 1 = second; -1 if none. */
static int current_copy(FIL *f,uint32_t id,BYTE mode,uint32_t *count,uint32_t *gen){
 if(!open_copy(f,id,0,mode,count,gen))return 0;
 if(!open_copy(f,id,1,mode,count,gen))return 1;
 return -1;
}
static int open_on(FIL *f,uint32_t id,BYTE mode,uint32_t *count){
 uint32_t gen;return current_copy(f,id,mode,count,&gen)<0?-1:0;
}

/* The read handle `ef` for playlist `id` (kept open, v1.6). */
static int entry_file(uint32_t id){
 if(ef_open&&ef_id==id)return 0;
 if(ef_open){f_close(&ef);ef_open=0;}
 uint32_t n;if(open_on(&ef,id,FA_READ,&n))return -1;
 ef_open=1;ef_id=id;ef_count=n;return 0;
}
unsigned playlist_length(uint32_t id){return entry_file(id)?0:ef_count;}
static uint8_t sec[512];
static void pack(const char *path,const char *title,const char *artist,uint32_t ms){
 memset(sec,0,512);
 const char *rel=memcmp(path,"0:/gbamp3/",10)?path:path+10;
 unsigned n=strlen(rel);if(n>STORAGE_PATH-11)n=STORAGE_PATH-11;memcpy(sec,rel,n);
 n=strlen(title);if(n>=META_TITLE)n=META_TITLE-1;memcpy(sec+256,title,n);
 n=strlen(artist);if(n>=META_NAME)n=META_NAME-1;memcpy(sec+384,artist,n);
 sec[480]=(uint8_t)ms;sec[481]=(uint8_t)(ms>>8);sec[482]=(uint8_t)(ms>>16);sec[483]=(uint8_t)(ms>>24);
 uint32_t h=fnv(sec,508);sec[508]=(uint8_t)h;sec[509]=(uint8_t)(h>>8);sec[510]=(uint8_t)(h>>16);sec[511]=(uint8_t)(h>>24);
}
int playlist_exists(const char *path);
static int entry_read(uint32_t id,unsigned i,struct PlaylistEntry *e,int check){
 memset(e,0,sizeof(*e));
 if(entry_file(id))return -1;
 int r=i<ef_count?at(&ef,(1+i)*512,esec,512):-1;if(r){f_close(&ef);ef_open=0;return -1;}
 uint32_t h=(uint32_t)esec[508]|(uint32_t)esec[509]<<8|(uint32_t)esec[510]<<16|(uint32_t)esec[511]<<24;
 if(h!=fnv(esec,508)){strcpy(e->title,"?");return 0;}
 memcpy(e->path,"0:/gbamp3/",10);memcpy(e->path+10,esec,246);e->path[STORAGE_PATH-1]=0;
 memcpy(e->title,esec+256,META_TITLE-1);memcpy(e->artist,esec+384,META_NAME-1);
 e->duration_ms=(uint32_t)esec[480]|(uint32_t)esec[481]<<8|(uint32_t)esec[482]<<16|(uint32_t)esec[483]<<24;
 e->valid=check?playlist_exists(e->path):1;return 0;
}
int playlist_exists(const char *path){static FILINFO fi;return f_stat(path,&fi)==FR_OK;}
int playlist_entry(uint32_t id,unsigned i,struct PlaylistEntry *e){return entry_read(id,i,e,1);}
int playlist_entry_quick(uint32_t id,unsigned i,struct PlaylistEntry *e){return entry_read(id,i,e,0);}
/* One change to the entries of an open copy that holds `n` entries. Kinds:
 * add (the entry is in `other`), remove a, swap a/b, rename paths (old, nw,
 * dir; also only counts the matches when `count_only`). New count in *out. */
enum {OP_ADD,OP_REMOVE,OP_SWAP,OP_RENAME,OP_PRUNE};
/* 11.0 OP_PRUNE: the entries [a, a+b) marked in `gone` are dropped; on the
 * main copy (the last one changed) each is reported to `pruned` with its
 * index at that moment (ascending: as single removals one after another). */
#define PRUNE_WINDOW 8192u
static uint8_t gone[PRUNE_WINDOW/8];
static void (*pruned)(uint32_t id,uint32_t at);static uint32_t prune_id;static unsigned prune_report;
static int rename_entry(const char *old,const char *nw,unsigned dir){
 /* `sec` holds an entry: 1 if it was renamed in place, 0 if not concerned. */
 if(((uint32_t)sec[508]|(uint32_t)sec[509]<<8|(uint32_t)sec[510]<<16|(uint32_t)sec[511]<<24)!=fnv(sec,508))return 0;
 static char full[STORAGE_PATH];unsigned a=strlen(old),b=strlen(nw);
 memcpy(full,"0:/gbamp3/",10);memcpy(full+10,sec,246);full[STORAGE_PATH-1]=0;
 if(memcmp(full,old,a)||(full[a]&&!(dir&&full[a]=='/')))return 0;
 unsigned c=strlen(full+a);if(b+c>=STORAGE_PATH-1)return 0;
 memmove(full+b,full+a,c+1);memcpy(full,nw,b);
 const char *rel=memcmp(full,"0:/gbamp3/",10)?full:full+10;unsigned r=strlen(rel);if(r>STORAGE_PATH-11)return 0;
 memset(sec,0,256);memcpy(sec,rel,r);
 uint32_t h=fnv(sec,508);sec[508]=(uint8_t)h;sec[509]=(uint8_t)(h>>8);sec[510]=(uint8_t)(h>>16);sec[511]=(uint8_t)(h>>24);
 return 1;
}
static int apply(FIL *f,uint32_t n,uint32_t *out_n,unsigned kind,unsigned a,unsigned b,const char *old,const char *nw,unsigned dir,int count_only){
 int err=0;*out_n=n;
 if(kind==OP_ADD){err=put(f,(1+n)*512,other,512);*out_n=n+1;}
 else if(kind==OP_REMOVE){
  if(a>=n)return -1;
  for(unsigned k=a+1;!err&&k<n;k++){err=at(f,(1+k)*512,sec,512)||put(f,k*512,sec,512);if(!(k&15))folder_yield();}
  *out_n=n-1;
 }else if(kind==OP_SWAP){
  if(a>=n||b>=n)return -1;
  err=at(f,(1+a)*512,sec,512)||at(f,(1+b)*512,other,512)||put(f,(1+a)*512,other,512)||put(f,(1+b)*512,sec,512);
 }else if(kind==OP_PRUNE){
  uint32_t w=a;
  for(uint32_t k=a;!err&&k<n;k++){
   if(k<a+b&&(gone[(k-a)>>3]>>((k-a)&7)&1)){if(prune_report&&pruned)pruned(prune_id,w);continue;}
   if(w!=k)err=at(f,(1+k)*512,sec,512)||put(f,(1+w)*512,sec,512);
   w++;if(!(k&15))folder_yield();
  }
  *out_n=w;
 }else{
  unsigned found=0;
  for(unsigned k=0;!err&&k<n;k++){
   if(at(f,(1+k)*512,sec,512)){err=-1;break;}
   if(rename_entry(old,nw,dir)){found++;if(!count_only)err=put(f,(1+k)*512,sec,512);}
   if(!(k&15))folder_yield();
  }
  if(count_only&&!err)*out_n=found;
 }
 return err?-1:0;
}
/* Copy every entry of copy `from` into copy `to` (made invalid first), then
 * give it the same header: both copies equal again. */
static int copy_over(uint32_t id,unsigned from,unsigned to,uint32_t n,uint32_t gen){
 if(f_open(&out,pfile_copy(id,from),FA_READ)!=FR_OK)return -1;
 int err=f_open(&pf,pfile_copy(id,to),FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK;
 if(err){f_close(&out);return -1;}
 memset(sec,0,512);err=put(&pf,0,sec,512)||f_sync(&pf)!=FR_OK;
 for(unsigned k=0;!err&&k<n;k++){err=at(&out,(1+k)*512,sec,512)||put(&pf,(1+k)*512,sec,512);if(!(k&15))folder_yield();}
 if(!err)err=f_sync(&pf)!=FR_OK||write_file_head(&pf,id,n,gen);
 if(f_close(&pf)!=FR_OK)err=-1;f_close(&out);return err?-1:0;
}
static int modify(uint32_t id,unsigned kind,unsigned a,unsigned b,const char *old,const char *nw,unsigned dir){
 if(ef_open){f_close(&ef);ef_open=0;}   /* readers reopen the valid copy */
 uint32_t n,gen,on=0,og=0;
 int src=current_copy(&pf,id,FA_READ,&n,&gen);if(src<0)return -1;f_close(&pf);
 unsigned dst=(unsigned)src^1;
 int ook=!open_copy(&pf,id,dst,FA_READ,&on,&og);if(ook)f_close(&pf);
 if(kind==OP_RENAME){   /* nothing to do when no entry has the old path */
  uint32_t found;if(f_open(&pf,pfile_copy(id,(unsigned)src),FA_READ)!=FR_OK)return -1;
  int e=apply(&pf,n,&found,kind,a,b,old,nw,dir,1);f_close(&pf);if(e)return -1;if(!found)return 0;
 }
 if((!ook||og!=gen||on!=n)&&copy_over(id,(unsigned)src,dst,n,gen))return -1;
 for(unsigned step=0;step<2;step++){
  unsigned c=step?0u:1u;uint32_t nn;   /* the second copy first, the main copy last */
  if(ef_open){f_close(&ef);ef_open=0;}
  if(f_open(&pf,pfile_copy(id,c),FA_READ|FA_WRITE)!=FR_OK)return -1;
  memset(sec,0,512);int err=put(&pf,0,sec,512)||f_sync(&pf)!=FR_OK;   /* this copy is invalid now */
  prune_report=step;   /* OP_PRUNE: report the removals once, on the main copy */
  if(!err)err=apply(&pf,n,&nn,kind,a,b,old,nw,dir,0);
  if(!err)err=f_sync(&pf)!=FR_OK||write_file_head(&pf,id,nn,gen+1);
  if(f_close(&pf)!=FR_OK)err=-1;
  if(err)return -1;
 }
 return 0;
}
static int playlist_add_do(uint32_t id,const char *path,const char *title,const char *artist,uint32_t ms){
 pack(path,title,artist,ms);memcpy(other,sec,512);
 disk_index_writes(1);int err=modify(id,OP_ADD,0,0,0,0,0);disk_index_writes(0);return err;
}
int playlist_add(uint32_t id,const char *path,const char *title,const char *artist,uint32_t ms){close_reads();int r=playlist_add_do(id,path,title,artist,ms);close_reads();return r;}
int playlist_remove(uint32_t id,unsigned i){
 close_reads();disk_index_writes(1);int r=modify(id,OP_REMOVE,i,0,0,0,0);disk_index_writes(0);close_reads();return r;
}
int playlist_swap(uint32_t id,unsigned i,unsigned j){
 close_reads();disk_index_writes(1);int r=modify(id,OP_SWAP,i,j,0,0,0);disk_index_writes(0);close_reads();return r;
}
/* v1.9: a renamed song file (dir 0) or folder (dir 1): entries for it, or
 * inside it, take the new path. Titles stay (they come from the tags). */
int playlist_rename_paths(const char *old,const char *new,unsigned dir){
 close_reads();disk_index_writes(1);int err=0;
 for(unsigned i=0;i<playlist_count();i++){uint32_t id;if(playlist_info(i,&id,0)){err=-1;continue;}close_reads();if(modify(id,OP_RENAME,0,0,old,new,dir))err=-1;}
 disk_index_writes(0);close_reads();return err;
}
/* 11.0 (Update Library, after a successful scan): entries whose song file
 * is absent are removed. Only f_stat's FR_NO_FILE / FR_NO_PATH count as
 * absent; an entry whose sector fails its checksum is kept, and any other
 * result (a read error) stops: nothing more is removed from this playlist
 * (-1). Windows of PRUNE_WINDOW entries are checked first, read only, then
 * removed in one change (power-cut safe as every change: old or new list).
 * known(path) 1: the file is in the library just scanned (kept without
 * searching its folder on the card; 0 / no function: f_stat decides).
 * *removed: the entries removed. */
int playlist_prune(uint32_t id,int (*known)(const char *path),void (*on_removed)(uint32_t id,uint32_t at),unsigned *removed){
 static char full[STORAGE_PATH];static FILINFO fi;
 close_reads();*removed=0;int err=0;uint32_t start=0;
 for(;;){
  uint32_t n,gen;if(current_copy(&pf,id,FA_READ,&n,&gen)<0){err=-1;break;}
  uint32_t len=n-start<PRUNE_WINDOW?n-start:PRUNE_WINDOW,absent=0;memset(gone,0,sizeof(gone));
  for(uint32_t k=0;k<len&&!err;k++){
   if(at(&pf,(1+start+k)*512,sec,512)){err=-1;break;}
   if(((uint32_t)sec[508]|(uint32_t)sec[509]<<8|(uint32_t)sec[510]<<16|(uint32_t)sec[511]<<24)!=fnv(sec,508))continue;
   memcpy(full,"0:/gbamp3/",10);memcpy(full+10,sec,246);full[STORAGE_PATH-1]=0;
   if(known&&known(full))continue;   /* in the library just scanned: there (no folder search) */
   FRESULT r=f_stat(full,&fi);   /* the path the player reads (entry_read) */
   if(r==FR_NO_FILE||r==FR_NO_PATH){gone[k>>3]|=(uint8_t)(1u<<(k&7));absent++;}
   else if(r!=FR_OK&&r!=FR_INVALID_NAME)err=-1;   /* a read error: stop, keep the rest */
   if(!(k&15))folder_yield();
  }
  f_close(&pf);
  if(err)break;
  if(absent){
   pruned=on_removed;prune_id=id;disk_index_writes(1);int e=modify(id,OP_PRUNE,start,len,0,0,0);disk_index_writes(0);pruned=0;
   if(e){err=-1;break;}
   *removed+=absent;
  }
  start+=len-absent;
  if(start>=n-absent)break;
 }
 close_reads();return err;
}
