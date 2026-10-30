/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder) (shine: LGPL v2, player/vendor/shine). GAC -> MP3
 * conversion on the GBA. See convert.h. */
#include "convert.h"
#include "gac4.h"
#include "meta.h"
#include "ff.h"
#if defined(__arm__)   /* the MP3 encoder overlay's own copy (build.py) */
#define resample_run resample_run_m
#define resample_begin resample_begin_m
#define resample_begin_down resample_begin_down_m
#endif
#include "resample.h"
#include "jpeg_enc.h"
#include "layer3.h"
#include "types.h"
#include <string.h>

#if defined(__arm__)
static uint32_t now(void){uint32_t h,l,h2;do{h=*(volatile uint16_t*)0x400010c;l=*(volatile uint16_t*)0x4000108;h2=*(volatile uint16_t*)0x400010c;}while(h!=h2);return h<<16|l;}
#else
static uint32_t now(void){return 0;}
#endif

struct GWork {
 FIL src,out;
 struct GacInfo info;
 struct SongMeta meta;
 struct Resampler rs;
 uint32_t block,blocks_left,frames_left;   /* next block to read; frames of the song left to decode */
 uint32_t avail,in_done,total_in;
 int16_t res[2*1152];unsigned resn;
 uint32_t hash,written;
 uint8_t head[GAC_HEADER+512];
 uint16_t art[64*64];
 char path[264];
 FILINFO fi;
};
/* The encoder (or, before it starts, the ID3 tag being built) follows. */
#define ENC_AT ((sizeof(struct GWork)+7u)&~7u)
_Static_assert(ENC_AT+sizeof(shine_global_config)<=CONVERT_GAC_WORK_BYTES,"CONVERT_GAC_WORK_BYTES");
_Static_assert(sizeof(shine_global_config)>=1024u+JPEG_FLAT_MAX,"tag area");

static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t fnv(uint32_t h,const uint8_t *p,unsigned n){while(n--)h=(h^*p++)*16777619u;return h;}
static int wr(struct GWork *w,const void *p,unsigned n){UINT g=0;if(f_write(&w->out,p,n,&g)!=FR_OK||g!=n)return 1;w->hash=fnv(w->hash,p,n);w->written+=n;return 0;}

/* ---------------------------------------------------------------- ID3v2.4 */
static unsigned syncsafe(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>21&127);p[1]=(uint8_t)(v>>14&127);p[2]=(uint8_t)(v>>7&127);p[3]=(uint8_t)(v&127);return 4;}
static unsigned frame_head(uint8_t *p,const char *id,uint32_t size){memcpy(p,id,4);syncsafe(p+4,size);p[8]=p[9]=0;return 10;}
static unsigned text_frame(uint8_t *p,const char *id,const char *s){
 unsigned n=strlen(s);if(!n)return 0;
 unsigned k=frame_head(p,id,n+1u);p[k++]=3;memcpy(p+k,s,n);return k+n;   /* UTF-8 */
}
static unsigned number(char *o,unsigned v,unsigned of){
 char t[12];unsigned n=0,k=0;do{t[n++]=(char)('0'+v%10);v/=10;}while(v);while(n)o[k++]=t[--n];
 (void)of;o[k]=0;return k;
}
/* The tag (song fields, front cover) in t; returns its size or 0. */
static unsigned id3_tag(const struct SongMeta *m,const uint16_t *art,uint8_t *t,unsigned cap){
 unsigned k=10;char num[12];
 k+=text_frame(t+k,"TIT2",m->title_from_name?"":m->title);
 k+=text_frame(t+k,"TPE1",m->artist);
 k+=text_frame(t+k,"TALB",m->album);
 k+=text_frame(t+k,"TPE2",m->album_artist);
 k+=text_frame(t+k,"TCON",m->genre);
 if(m->track){number(num,m->track,0);k+=text_frame(t+k,"TRCK",num);}
 if(m->disc){number(num,m->disc,0);k+=text_frame(t+k,"TPOS",num);}
 if(art){
  static const char mime[]="image/jpeg";
  unsigned h=10+1+sizeof(mime)+1+1;   /* frame head, encoding, MIME, type, empty description */
  unsigned n=k+h<cap?jpeg_flat_cover(art,t+k+h,cap-k-h):0;
  if(n){unsigned p=k;p+=frame_head(t+p,"APIC",h-10+n);t[p++]=0;memcpy(t+p,mime,sizeof(mime));p+=sizeof(mime);t[p++]=3;t[p++]=0;k=p+n;}
 }
 memcpy(t,"ID3",3);t[3]=4;t[4]=0;t[5]=0;syncsafe(t+6,k-10u);
 return k;
}

/* ------------------------------------------------------------- the audio */
static void cleanup(struct GWork *w){f_close(&w->src);f_close(&w->out);f_unlink(w->path);}
/* Decode GAC into inL/inR until about cap frames are there (GAC overlay). */
static int decode_chunk(struct GWork *w,int16_t *inL,int16_t *inR,unsigned cap){
 while(w->frames_left&&w->avail+GAC_UNIT<=cap){
  if(!w->blocks_left){
   if(w->block>=w->info.blocks)return CONVERT_READ;
   UINT got=0;
   if(f_lseek(&w->src,w->info.audio+GAC_BLOCK*w->block)!=FR_OK||f_read(&w->src,gac4.block,GAC_BLOCK,&got)!=FR_OK||got!=GAC_BLOCK)return CONVERT_READ;
   w->block++;w->blocks_left=gac_block_frames((const unsigned char*)gac4.block);
   if(w->blocks_left>w->frames_left)w->blocks_left=w->frames_left;
   gac4_begin(w->info.channels,0);
  }
  unsigned units=(cap-w->avail)/GAC_UNIT;
  unsigned n=gac4_decode16(units,w->blocks_left,inL+w->avail,inR+w->avail);
  if(!n)return CONVERT_READ;
  w->avail+=n;w->blocks_left-=n;w->frames_left-=n;w->in_done+=n;
 }
 return 0;
}
/* Resample to 32000 Hz and encode (conversion overlay). */
static int encode_chunk(struct GWork *w,shine_t s,int16_t *inL,int16_t *inR,int flush){
 for(;;){
  uint32_t t0=now();
  unsigned n=resample_run(&w->rs,inL,inR,w->avail,flush,w->res+2*w->resn,1152u-w->resn);
  convert_stats.cyc_resample+=now()-t0;
  w->resn+=n;
  if(w->resn==1152u||(flush&&!n&&w->resn)){
   for(unsigned i=w->resn;i<1152u;i++)w->res[2*i]=w->res[2*i+1]=0;
   int got=0;t0=now();unsigned char *d=shine_encode_buffer_interleaved(s,w->res,&got);convert_stats.cyc_encode+=now()-t0;
   convert_stats.frames_out+=w->resn;w->resn=0;convert_stats.blocks++;
   if(got>0&&wr(w,d,(unsigned)got))return CONVERT_WRITE;
   continue;
  }
  if(!n)break;
 }
 uint32_t keep=w->rs.base>7?w->rs.base-7:0;if(keep>w->avail)keep=w->avail;
 if(keep){memmove(inL,inL+keep,(w->avail-keep)*2u);memmove(inR,inR+keep,(w->avail-keep)*2u);w->avail-=keep;w->rs.base-=keep;}
 return 0;
}

int convert_gac_to_mp3(const char *src,const char *dst,uint8_t *work,int16_t *inL,int16_t *inR,unsigned cap,
                       convert_use_fn use,convert_poll_fn poll,void *ctx){
 struct GWork *w=(struct GWork*)(void*)work;memset(w,0,sizeof(*w));memset(&convert_stats,0,sizeof(convert_stats));
 {extern struct GacEnc *conv_enc_dbg;conv_enc_dbg=0;}
 uint8_t *enc=work+ENC_AT;
 unsigned dl=strlen(dst);if(dl+6>sizeof(w->path))return CONVERT_WRITE;
 memcpy(w->path,dst,dl);strcpy(w->path+dl,".part");
 if(f_stat(dst,&w->fi)==FR_OK)return CONVERT_EXISTS;
 if(f_open(&w->src,src,FA_READ)!=FR_OK)return CONVERT_READ;
 UINT got=0;
 if(f_read(&w->src,w->head,sizeof(w->head),&got)!=FR_OK||got<GAC_HEADER||gac_header(w->head,GAC_HEADER,(unsigned)f_size(&w->src),&w->info)!=1||w->info.rate!=32768||!w->info.total){f_close(&w->src);return CONVERT_NO_AUDIO;}
 /* Tags and cover from the GAC file itself. */
 const char *b=src;for(const char *q=src;*q;q++)if(*q=='/')b=q+1;
 if(meta_read(&w->src,b,&w->meta)){f_close(&w->src);return CONVERT_READ;}
 int has_art=0;
 if(w->info.art&&w->info.art_bytes==8192u&&f_lseek(&w->src,w->info.art)==FR_OK&&f_read(&w->src,w->art,8192,&got)==FR_OK&&got==8192&&
    gac_cover_valid(w->head,(const unsigned char*)w->art))has_art=1;
 /* NAME.part: the tag, then the frames as they come; read back at the end. */
 if(f_open(&w->out,w->path,FA_CREATE_ALWAYS|FA_WRITE|FA_READ)!=FR_OK){f_close(&w->src);return CONVERT_WRITE;}
 w->hash=2166136261u;
 unsigned tl=id3_tag(&w->meta,has_art?w->art:0,enc,(unsigned)sizeof(shine_global_config));
 if(wr(w,enc,tl)){cleanup(w);return CONVERT_WRITE;}
 shine_config_t cf;memset(&cf,0,sizeof(cf));
 cf.wave.channels=PCM_STEREO;cf.wave.samplerate=32000;cf.mpeg.mode=JOINT_STEREO;cf.mpeg.bitr=CONVERT_MP3_KBPS;cf.mpeg.emph=NONE;cf.mpeg.original=1;
 if(use)use(1);   /* shine_initialise calls into the overlay */
 shine_t s=shine_initialise(&cf,enc);
 if(!s){if(use)use(0);cleanup(w);return CONVERT_WRITE;}
 w->frames_left=w->info.total;w->total_in=w->info.total;
 w->avail=7;for(unsigned i=0;i<7;i++)inL[i]=inR[i]=0;   /* the resampler's first taps */
 int r=0;
 while(!r){
  if(use)use(0);
  uint32_t t0=now();r=decode_chunk(w,inL,inR,cap);convert_stats.cyc_decode+=now()-t0;convert_stats.frames_in=w->in_done;
  if(r)break;
  if(use)use(1);
  if(!w->rs.tab)resample_begin_down(&w->rs);
  int flush=!w->frames_left;
  r=encode_chunk(w,s,inL,inR,flush);
  if(!r&&flush){
   /* One frame of silence (the encoder's delay, about 1100 frames, holds
    * the song's end back), with the encoder overlay still loaded. */
   memset(w->res,0,sizeof(w->res));int n=0;unsigned char *d=shine_encode_buffer_interleaved(s,w->res,&n);
   if(n>0&&wr(w,d,(unsigned)n))r=CONVERT_WRITE;
   if(!r){d=shine_flush(s,&n);if(n>0&&wr(w,d,(unsigned)n))r=CONVERT_WRITE;}
  }
  if(!r&&poll&&poll(ctx,w->in_done,w->total_in)){if(use)use(0);cleanup(w);return CONVERT_CANCELLED;}
  if(flush)break;
 }
 if(use)use(0);
 if(r){cleanup(w);return r;}
 shine_close(s);
 f_close(&w->src);
 /* Read the file back. */
 if(f_sync(&w->out)!=FR_OK||f_lseek(&w->out,0)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 uint32_t h=2166136261u,left=w->written;
 while(left){unsigned k=left<8192u?left:8192u;if(f_read(&w->out,enc,k,&got)!=FR_OK||got!=k){cleanup(w);return CONVERT_VERIFY;}h=fnv(h,enc,k);left-=k;
  if(poll&&poll(ctx,w->total_in,w->total_in)){cleanup(w);return CONVERT_CANCELLED;}}
 if(h!=w->hash||f_size(&w->out)!=w->written){cleanup(w);return CONVERT_VERIFY;}
 if(f_close(&w->out)!=FR_OK){f_unlink(w->path);return CONVERT_WRITE;}
 if(f_rename(w->path,dst)!=FR_OK){f_unlink(w->path);return CONVERT_EXISTS;}
 convert_stats.rate=32000;
 return CONVERT_OK;
}
