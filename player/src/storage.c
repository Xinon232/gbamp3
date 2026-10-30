/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Sequential MP3/GAC file input by path; no writes.
 * v0.8.0: folder browsing moved to folder.c; songs are opened by full path. */
#include "storage.h"
#include "mp3_header.h"
#include "cover.h"
#include "ff.h"
#include <string.h>
static FATFS fs;
void storage_discard_window(void){fs.wflag=0;fs.winsect=(LBA_t)0-1;fs.fsi_flag&=0x80;}
static FIL file;
static char open_path[STORAGE_PATH];
static unsigned opened,base,pos,valid,end,expected_rate,expected_channels,open_key;
static unsigned char buffer[8192+8] __attribute__((aligned(4)));
static int error;
static unsigned cache_kind,cache_block,cache_frame;
/* v1.8: two 64x64 cover slots (8 KB each) in video memory no screen uses:
 * past the second mode 4 page (0x06013600) up to the letter box sprite
 * (0x06017800). Work RAM held two 32x32 covers before. */
#ifdef __arm__
#define COVER(i) ((volatile unsigned short*)(0x06013600u+(i)*COVER_BYTES_64))
#else
static unsigned short cover_ram[2][COVER_PIXELS];
#define COVER(i) ((volatile unsigned short*)cover_ram[i])
#endif
static unsigned cover_keys[2],cover_valid[2],cover_slot;
const volatile unsigned short*storage_cover(unsigned key){for(unsigned i=0;i<2;i++)if(cover_valid[i]&&cover_keys[i]==key)return COVER(i);return 0;}
volatile unsigned short *storage_cover_slot(unsigned key){unsigned slot=cover_slot;cover_slot^=1;cover_valid[slot]=0;cover_keys[slot]=key;return COVER(slot);}
void storage_cover_ready(unsigned key){for(unsigned i=0;i<2;i++)if(cover_keys[i]==key)cover_valid[i]=1;}
static struct GacInfo cache_info;
static unsigned cache_name(const char *s){unsigned n=strlen(s);return n>=4&&s[n-4]=='.'&&(s[n-3]=='g'||s[n-3]=='G')&&(s[n-2]=='a'||s[n-2]=='A')&&(s[n-1]=='c'||s[n-1]=='C');}
unsigned storage_path_is_cache(const char *path){return cache_name(path);}
unsigned storage_is_cache(void){return cache_kind;}
const struct GacInfo *storage_cache_info(void){return &cache_info;}
unsigned storage_key(const char *path){unsigned h=2166136261u;while(*path)h=(h^(unsigned char)*path++)*16777619u;return h|1u;}
#define SEEK_POINTS 1024
/* A point every 128 frames: point j is frame ep[j]*128 at offset off[j]
 * (0xffff: empty; v2.2 packed, 4 KB less than frame+offset words). */
/* v2.2 GAC: checked marks a seek table checked at open (FNV-1a32 of the
 * header, never 0) for a file of this size and first cluster. */
struct SeekIndex {unsigned key,total,size,clust,checked;struct MP3Header h;unsigned off[SEEK_POINTS];uint16_t ep[SEEK_POINTS];};
static struct SeekIndex indexes[2];
static unsigned selected,next_slot,stream_frame;
static void reset_index(struct SeekIndex*x,unsigned key){
 memset(x,0,sizeof(*x));x->key=key;for(unsigned j=0;j<SEEK_POINTS;j++)x->ep[j]=0xffffu;
}
FATFS *storage_fs(void){return &fs;}
FIL *storage_file(void){return opened?&file:0;}
unsigned storage_position(void){return base+pos;}
unsigned storage_size(void){return end;}
static int refill(unsigned needed){
 if(error)return error;
 if(valid-pos>=needed)return 1;
 unsigned left=valid-pos;memmove(buffer,buffer+pos,left);base+=pos;pos=0;valid=left;
 unsigned n=sizeof(buffer)-8-left;if(n>end-base-left)n=end-base-left;
 if(n){UINT got=0;FRESULT r=f_read(&file,buffer+left,n,&got);if(r!=FR_OK||got!=n)return error=-1;valid+=got;}
 memset(buffer+valid,0,8);return valid>=needed?1:0;
}
int storage_init(void){
 cover_valid[0]=cover_valid[1]=0;
 if(opened)f_close(&file);
 opened=0;error=0;reset_index(&indexes[0],0);reset_index(&indexes[1],0);next_slot=0;
 f_mount(0,"0:",0);
 if(f_mount(&fs,"0:",1)!=FR_OK)return -2;
 return 0;
}
/* Before files are renamed, deleted or rewritten: seek tables are checked again. */
void storage_close(void){if(opened){f_close(&file);opened=0;}indexes[0].checked=indexes[1].checked=0;}
/* v2.2 fast seek: a cluster link map of the open song (63 fragments), so a
 * seek finds its cluster at once instead of walking the FAT chain from the
 * file start (86 ms deep into a long GAC on 4 KB clusters). Making it walks
 * the whole chain (0.6 s for 12 MB on 512 B clusters, with no key poll), so
 * not at open (songs start, and follow each other gaplessly, as before) but
 * at the first seek that walks at least an eighth of the file. A more
 * fragmented file seeks the normal way. */
static DWORD link_map[128];
static unsigned map_tried;
int storage_open(const char *path){
 /* v2.2: the open song again (a seek, a restart): rewound, not reopened. */
 if(!(opened&&error!=-1&&!strcmp(path,open_path)&&f_lseek(&file,0)==FR_OK)){
  if(opened){FRESULT r=f_close(&file);opened=0;if(r!=FR_OK)return -1;}
  unsigned n=strlen(path);if(n>=STORAGE_PATH||memcmp(path,"0:/gbamp3/",10))return -2;
  if(f_open(&file,path,FA_READ)!=FR_OK)return -1;
  memcpy(open_path,path,n+1);opened=1;map_tried=0;
 }
 error=0;unsigned key=storage_key(path);open_key=key;
 stream_frame=0;cache_kind=cache_name(path);cache_block=0;
 if(indexes[0].key==key)selected=0;else if(indexes[1].key==key)selected=1;
 else{selected=next_slot;next_slot^=1;reset_index(&indexes[selected],key);}
 end=f_size(&file);base=pos=valid=0;expected_rate=expected_channels=0;
 if(end<4||end>0x7fffffffu)return error=-3;
 if(cache_kind){
  int r=refill(64);if(r<0)return r;
  int h=r?gac_header(buffer,64,end,&cache_info):0;
  if(h<0)return error=-4;                  /* GAC v1..v3: reconvert */
  if(h!=1)return error=-3;
  if(refill(576)!=1||!gac_metadata_valid(buffer,buffer+64))return error=-3;
  static unsigned char head[64];memcpy(head,buffer,64);
  /* The whole seek table is checked at open: a bad table rejects the file.
   * v2.2: once per file (path, size, first cluster and header unchanged):
   * a 10 MB file took 0.34 s, at every seek. */
  struct SeekIndex*x=&indexes[selected];unsigned sig=gac_fnv(2166136261u,head,64)|1u;
  if(x->checked!=sig||x->size!=end||x->clust!=(unsigned)file.obj.sclust){
   x->checked=0;struct GacSeekCheck c={0,0,0};
   for(unsigned j=0;j<cache_info.blocks;){
    unsigned n=cache_info.blocks-j;if(n>1024)n=1024;
    if(storage_at(cache_info.seek+4*j)||(r=refill(4*n))<0)return error=-1;
    if(!r||!gac_seek_check(&c,buffer+pos,n,&cache_info))return error=-3;
    j+=n;
   }
   if(!gac_seek_done(&c,head,&cache_info))return error=-3;
   x->checked=sig;x->size=end;x->clust=(unsigned)file.obj.sclust;
  }
  cache_block=cache_frame=0;
  return storage_at(cache_info.audio);
 }
 int r=refill(10);if(r<0)return r;
 if(valid>=3&&!memcmp(buffer,"ID3",3)){
  if(valid<10||buffer[3]<2||buffer[3]>4||buffer[4]==255||(buffer[6]|buffer[7]|buffer[8]|buffer[9])&128)return error=-3;
  unsigned skip=10+((unsigned)buffer[6]<<21)+((unsigned)buffer[7]<<14)+((unsigned)buffer[8]<<7)+buffer[9];
  if(buffer[3]==4&&(buffer[5]&16))skip+=10;
  if(skip>end-4)return error=-3;
  if(f_lseek(&file,skip)!=FR_OK||f_tell(&file)!=skip)return error=-1;
  base=skip;pos=valid=0;
 }
 return 0;
}
int storage_at(unsigned offset){
 if(!opened||offset>=end)return -3;
 unsigned from=offset<f_tell(&file)?0:f_tell(&file);
 if(!map_tried&&offset-from>=end/8){
  map_tried=1;link_map[0]=sizeof(link_map)/sizeof(link_map[0]);file.cltbl=link_map;
  if(f_lseek(&file,CREATE_LINKMAP)!=FR_OK)file.cltbl=0;
 }
 if(f_lseek(&file,offset)!=FR_OK||f_tell(&file)!=offset)return error=-1;
 base=offset;pos=valid=0;error=0;return 0;
}
int storage_next(unsigned char **data,unsigned *bytes){
 if(cache_kind)return -3;
 if(error)return error;
 if(!opened)return -2;
 int r=refill(4);if(r<0)return r;
 if(!r){if(valid!=pos)return error=-3;indexes[selected].total=stream_frame;return 0;}
 unsigned remain=end-base-pos;
 if(remain==128&&!memcmp(buffer+pos,"TAG",3)){pos=valid;indexes[selected].total=stream_frame;return 0;}
 if(remain>=32&&refill(32)>0&&!memcmp(buffer+pos,"APETAGEX",8)){
  const unsigned char*p=buffer+pos;unsigned n=(unsigned)p[12]|((unsigned)p[13]<<8)|((unsigned)p[14]<<16)|((unsigned)p[15]<<24);
  if(n>=32&&(n==remain||(n<=0x7fffffdfu&&n+32==remain))){base=end;pos=valid=0;indexes[selected].total=stream_frame;return 0;}
  return error=-3;
 }
 if(error)return error;
 struct MP3Header h;if(!mp3_header(buffer+pos,&h))return error=-3;
 if(expected_rate&&(h.rate!=expected_rate||h.channels!=expected_channels))return error=-3;
 expected_rate=h.rate;expected_channels=h.channels;
 r=refill(h.bytes);if(r<0)return r;if(!r)return error=-3;
 struct SeekIndex*x=&indexes[selected];x->h=h;
 if(!(stream_frame&127u)&&stream_frame/128<0xffffu){unsigned j=(stream_frame/128)%SEEK_POINTS;x->ep[j]=(uint16_t)(stream_frame/128);x->off[j]=base+pos;}
 stream_frame++;*data=buffer+pos;*bytes=h.bytes;pos+=h.bytes;return 1;
}

int storage_cache_next(unsigned char **data,unsigned *frames){
 if(error)return error;
 if(!opened||!cache_kind)return -2;
 if(cache_block==cache_info.blocks||cache_frame>=cache_info.total)return 0;
 int r=refill(GAC_BLOCK);if(r<0)return r;if(!r)return error=-3;
 /* The block header holds its frame count; never past the file's total. */
 unsigned n=gac_block_frames(buffer+pos),left=cache_info.total-cache_frame;
 *frames=n<left?n:left;cache_frame+=*frames;*data=buffer+pos;pos+=GAC_BLOCK;cache_block++;return 1;
}
static int seek_entry(unsigned b,unsigned *v){
 unsigned char e[4];UINT got=0;
 if(f_lseek(&file,cache_info.seek+4*b)!=FR_OK||f_read(&file,e,4,&got)!=FR_OK||got!=4)return error=-1;
 *v=(unsigned)e[0]|((unsigned)e[1]<<8)|((unsigned)e[2]<<16)|((unsigned)e[3]<<24);return 0;
}
int storage_cache_seek(unsigned seconds,unsigned *sample){
 if(!opened||!cache_kind)return -2;
 return storage_cache_seek_frame(seconds>(cache_info.total-1)/cache_info.rate?cache_info.total-1:seconds*cache_info.rate,sample);
}
int storage_cache_seek_frame(unsigned target,unsigned *sample){
 if(!opened||!cache_kind)return -2;
 if(target>=cache_info.total)target=cache_info.total-1;
 /* Last block starting at or before the target (the table was checked at
  * open: 0 first, strictly increasing); the player discards the prefix. */
 unsigned lo=0,hi=cache_info.blocks-1,first=0;
 while(lo<hi){unsigned mid=lo+(hi-lo+1)/2,v;if(seek_entry(mid,&v))return -1;if(v<=target)lo=mid;else hi=mid-1;}
 if(lo&&seek_entry(lo,&first))return -1;
 cache_block=lo;cache_frame=first;*sample=first;
 return storage_at(cache_info.audio+lo*GAC_BLOCK);
}
int storage_seek_seconds(unsigned seconds,unsigned *warm,unsigned *target,struct MP3Header *h){
 if(cache_kind)return -3;
 if(!opened)return -2;
 struct SeekIndex*x=&indexes[selected];unsigned char*p;unsigned n;
 if(!x->h.rate){int r=storage_next(&p,&n);if(r!=1)return r<0?r:-3;}
 *h=x->h;
 unsigned t=(seconds/h->samples)*h->rate+(seconds%h->samples)*h->rate/h->samples;
 if(x->total&&t>=x->total)t=x->total-1;
 unsigned desired=t>12?t-12:0,best=0,at=~0u;
 for(unsigned j=0;j<SEEK_POINTS;j++){unsigned f=x->ep[j]*128u;if(x->ep[j]!=0xffffu&&f<=desired&&f>=best){best=f;at=x->off[j];}}
 if(at==~0u){
  /* Reopening begins immediately after ID3; old distant history may expire. */
  static char again[STORAGE_PATH];memcpy(again,open_path,sizeof(again));if(storage_open(again))return -1;best=0;at=storage_position();
 }
 if(storage_at(at))return -1;
 stream_frame=best;
 unsigned recent[13],seen=0,start=at;
 while(stream_frame<=t){
  int r=storage_next(&p,&n);if(r<0)return r;
  if(!r){if(!seen)return -3;t=stream_frame-1;break;}
  recent[(stream_frame-1)%13]=storage_position()-n;seen++;
  /* Bound compressed seek work; never turn a gesture into a whole-card scan. */
  if(storage_position()-start>2u*1024*1024)return -3;
 }
 unsigned w=t>12?t-12:0;
 if(w<best)return -3;
 if(storage_at(recent[w%13]))return -1;
 stream_frame=w;*warm=w;*target=t;return 0;
}
