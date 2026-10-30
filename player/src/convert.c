/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). MP3 -> GAC conversion on the GBA. See convert.h. */
#include "convert.h"
#include "gac_enc.h"
#include "ff.h"
#include "mp3dec.h"
#include <string.h>
#include "resample.h"

struct ConvertStats convert_stats;
struct GacEnc *conv_enc_dbg;   /* timing telemetry of test builds */
#if defined(__arm__)
static uint32_t now(void){uint32_t h,l,h2;do{h=*(volatile uint16_t*)0x400010c;l=*(volatile uint16_t*)0x4000108;h2=*(volatile uint16_t*)0x400010c;}while(h!=h2);return h<<16|l;}
#else
static uint32_t now(void){return 0;}
#endif

#define IN_BYTES 8192u
struct Work {
 struct GacEnc enc;
 FIL src,blk,sk,out;
 uint8_t in[IN_BYTES];
 int16_t pcm[2*1152];
 int16_t *inL,*inR;uint32_t avail,cap;   /* decoded frames at the source rate (caller's buffers) */
 struct Resampler rs;
 int16_t res[2*256];          /* resampled frames waiting for the encoder */
 uint32_t skq[128];           /* block start frames waiting for NAME.sk */
 uint8_t head[64+512];
 uint32_t blk_hash,blocks,skn;
 int werr;
 char path[3][264];           /* NAME.blk, NAME.sk, NAME.part */
 FILINFO fi;
 MP3FrameInfo frame;
};
_Static_assert(sizeof(struct Work)<=CONVERT_WORK_BYTES,"CONVERT_WORK_BYTES");

static uint32_t fnv(uint32_t h,const uint8_t *p,unsigned n){while(n--)h=(h^*p++)*16777619u;return h;}
static void le32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int wr(FIL *f,const void *p,unsigned n){UINT g=0;return f_write(f,p,n,&g)!=FR_OK||g!=n;}

/* Encoder output: blocks to NAME.blk, start frames to NAME.sk. */
static int put_block(void *ctx,const uint8_t b[1024],uint32_t first){
 struct Work *w=ctx;
 if(wr(&w->blk,b,1024)){w->werr=1;return -1;}
 w->blk_hash=fnv(w->blk_hash,b,1024);w->blocks++;
 w->skq[w->skn++]=first;
 if(w->skn==128){if(wr(&w->sk,w->skq,sizeof(w->skq))){w->werr=1;return -1;}w->skn=0;}
 return 0;
}

/* Resampled frames to the encoder (conversion overlay loaded). */
static int encode_chunk(struct Work *w,int flush){
 for(;;){
  uint32_t t0=now();unsigned n=resample_run(&w->rs,w->inL,w->inR,w->avail,flush,w->res,256);convert_stats.cyc_resample+=now()-t0;
  if(!n)break;
  for(unsigned i=0;i<2*n;i+=2){int32_t v=w->res[i];if(v<0)v=-v;convert_stats.peak_res+=(uint32_t)v>>4;}
  t0=now();int e=gac_enc_push(&w->enc,w->res,n);convert_stats.cyc_encode+=now()-t0;convert_stats.frames_out+=n;if(e)return e;
 }
 /* Keep the 7 frames before the next output frame's centre. */
 uint32_t keep=w->rs.base>7?w->rs.base-7:0;if(keep>w->avail)keep=w->avail;
 if(keep){memmove(w->inL,w->inL+keep,(w->avail-keep)*2u);memmove(w->inR,w->inR+keep,(w->avail-keep)*2u);w->avail-=keep;w->rs.base-=keep;}
 convert_stats.cyc_lpc=w->enc.cyc_lpc;convert_stats.cyc_sub=w->enc.cyc_sub;convert_stats.cyc_close=w->enc.cyc_close;convert_stats.frames_in=convert_stats.frames_in;
 return 0;
}

/* ------------------------------------------------------------- decoding */
static HMP3Decoder dec;
static __attribute__((noinline)) int conv_decode_init(void){dec=MP3InitDecoder();return !dec||MP3SetOutputMode(dec,3);}
/* One frame from w->in[*at..end); 0: decoded, 1: need more input (or the
 * end), -1: bad frame skipped. */
static __attribute__((noinline)) int conv_decode_frame(struct Work *w,unsigned *at,unsigned end,int eof,MP3FrameInfo *fi){
 int s=MP3FindSyncWord(w->in+*at,(int)(end-*at));if(s<0){*at=end>3?end-3:*at;return 1;}
 *at+=(unsigned)s;
 unsigned char *p=w->in+*at;int left=(int)(end-*at);
 if(left<1800&&!eof)return 1;                             /* the longest frame fits */
 if(left<4)return 1;
 int e=MP3Decode(dec,&p,&left,w->pcm,0);
 if(e){(*at)++;return -1;}
 *at=(unsigned)(p-w->in);MP3GetLastFrameInfo(dec,fi);return 0;
}

static void cleanup(struct Work *w){
 f_close(&w->src);f_close(&w->blk);f_close(&w->sk);f_close(&w->out);
 for(unsigned i=0;i<3;i++)f_unlink(w->path[i]);
}
static unsigned id3_size(const uint8_t *h){
 if(memcmp(h,"ID3",3)||(h[6]|h[7]|h[8]|h[9])&0x80)return 0;
 return 10u+((unsigned)h[6]<<21|(unsigned)h[7]<<14|(unsigned)h[8]<<7|h[9])+((h[5]&0x10)?10u:0u);
}

int convert_mp3_to_gac(const char *src,const char *dst,const struct SongMeta *meta,const uint16_t *art,uint8_t *work,
                       int16_t *inL,int16_t *inR,unsigned cap,convert_use_fn use,convert_poll_fn poll,void *ctx){
 struct Work *w=(struct Work*)(void*)work;memset(w,0,sizeof(*w));memset(&convert_stats,0,sizeof(convert_stats));
 w->inL=inL;w->inR=inR;w->cap=cap;w->avail=7;for(unsigned i=0;i<7;i++)inL[i]=inR[i]=0;   /* 7 zero frames: the first taps */
 unsigned dl=strlen(dst);if(dl+6>sizeof(w->path[0]))return CONVERT_WRITE;
 static const char *const ext[3]={".blk",".sk",".part"};
 for(unsigned i=0;i<3;i++){memcpy(w->path[i],dst,dl);strcpy(w->path[i]+dl,ext[i]);}
 if(f_stat(dst,&w->fi)==FR_OK)return CONVERT_EXISTS;
 if(f_open(&w->src,src,FA_READ)!=FR_OK)return CONVERT_READ;
 if(f_open(&w->blk,w->path[0],FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK||f_open(&w->sk,w->path[1],FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 w->blk_hash=2166136261u;
 gac_enc_begin(&w->enc,put_block,w);conv_enc_dbg=&w->enc;
 if(conv_decode_init()){cleanup(w);return CONVERT_NO_AUDIO;}
 uint32_t total_est=meta&&meta->duration_ms?(uint32_t)(((uint64_t)meta->duration_ms*32768u)/1000u):0;
 /* Skip the ID3v2 tag (pictures may hold false sync words). */
 UINT got=0;unsigned at=0,end=0;
 if(f_read(&w->src,w->in,10,&got)!=FR_OK){cleanup(w);return CONVERT_READ;}
 {unsigned skip=got==10?id3_size(w->in):0;if(f_lseek(&w->src,skip)!=FR_OK){cleanup(w);return CONVERT_READ;}}
 int eof=0,started=0,r=0,input_done=0;unsigned rate=0,chans=0;
 if(use)use(0);
 while(!r){
  /* Decode while a whole MP3 frame fits in the chunk (decoder overlay). */
  while(!input_done&&w->avail+1152u<=w->cap){
   if(!eof&&end-at<2048){memmove(w->in,w->in+at,end-at);end-=at;at=0;
    if(f_read(&w->src,w->in+end,IN_BYTES-end,&got)!=FR_OK){cleanup(w);if(use)use(0);return CONVERT_READ;}
    end+=got;if(!got||end<IN_BYTES)eof=1;
   }
   MP3FrameInfo *fi=&w->frame;
   uint32_t t0=now();int d=conv_decode_frame(w,&at,end,eof,fi);convert_stats.cyc_decode+=now()-t0;
   if(d==1){if(eof)input_done=1;continue;}
   if(d<0){convert_stats.bad_frames++;if(at>=end&&eof)input_done=1;continue;}
   if(!started){rate=(unsigned)fi->samprate;chans=(unsigned)fi->nChans;if(rate<8000||rate>48000||chans<1||chans>2){convert_stats.bad_frames++;continue;}
    started=1;convert_stats.rate=rate;}   /* resample_begin runs with its overlay loaded */
   if((unsigned)fi->samprate!=rate||(unsigned)fi->nChans!=chans){convert_stats.bad_frames++;continue;}
   unsigned n=(unsigned)fi->outputSamps/chans;
   for(unsigned i=0;i<n;i++){w->inL[w->avail+i]=w->pcm[chans*i];w->inR[w->avail+i]=w->pcm[chans*i+chans-1];
    int32_t v=w->pcm[chans*i];if(v<0)v=-v;convert_stats.peak_dec+=(uint32_t)v>>4;}
   w->avail+=n;convert_stats.frames_in+=n;
   if(poll&&poll(ctx,convert_stats.frames_out,total_est>convert_stats.frames_out?total_est:convert_stats.frames_out)){cleanup(w);return CONVERT_CANCELLED;}
   if(at>=end&&eof)input_done=1;
  }
  if(!started){if(input_done)break;continue;}
  /* Resample and encode the chunk (conversion overlay); at the end, every
   * frame up to the input's end: its length at 32768 Hz. */
  if(use)use(1);
  if(!w->rs.tab)resample_begin(&w->rs,rate);
  r=encode_chunk(w,input_done);
  if(use)use(0);
  if(input_done)break;
 }
 if(!r&&!started){cleanup(w);return CONVERT_NO_AUDIO;}
 if(!r){if(use)use(1);r=gac_enc_finish(&w->enc);if(use)use(0);}
 if(!r&&w->skn&&wr(&w->sk,w->skq,w->skn*4u))w->werr=1;
 f_close(&w->src);
 if(r||w->werr||f_close(&w->blk)!=FR_OK||f_close(&w->sk)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 convert_stats.blocks=w->blocks;
 uint32_t total=w->enc.total,count=w->blocks;
 /* ---- assemble NAME.part: header, metadata, cover, seek table, blocks. */
 uint8_t *h=w->head;memset(h,0,sizeof(w->head));uint8_t *m=h+64;
 memcpy(m,"GACMETA3",8);
 if(meta){m[8]=(uint8_t)meta->track;m[9]=(uint8_t)(meta->track>>8);m[12]=(uint8_t)meta->disc;m[13]=(uint8_t)(meta->disc>>8);
  static const unsigned short at_[5]={16,144,240,336,432},len_[5]={128,96,96,96,64};
  const char *f_[5]={meta->title,meta->artist,meta->album,meta->album_artist,meta->genre};
  for(unsigned i=0;i<5;i++){unsigned k=strlen(f_[i]);if(k>=len_[i])k=len_[i]-1u;memcpy(m+at_[i],f_[i],k);}}
 uint32_t art_len=art?8192u:0u,seek_at=576u+art_len,audio=(seek_at+4u*count+511u)&~511u;
 memcpy(h,"GBCACHE",8);h[8]=4;h[10]=64;le32(h+12,3);le32(h+16,32768);le32(h+20,2);le32(h+24,1024);le32(h+28,count);le32(h+32,total);
 le32(h+36,seek_at);le32(h+40,audio);le32(h+44,512);le32(h+48,art_len);le32(h+52,fnv(2166136261u,m,512));
 le32(h+56,art?fnv(2166136261u,(const uint8_t*)art,8192):0);
 if(f_open(&w->out,w->path[2],FA_CREATE_ALWAYS|FA_WRITE|FA_READ)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 int e=wr(&w->out,h,576)||(art&&wr(&w->out,art,8192));
 /* Seek table from NAME.sk (checksum into the header afterwards). */
 uint32_t sh=2166136261u;
 if(!e&&f_open(&w->sk,w->path[1],FA_READ)!=FR_OK)e=1;
 uint8_t *buf=(uint8_t*)w->enc.mid;const unsigned BUF=sizeof(w->enc.mid)+sizeof(w->enc.side);   /* the encoder is done: its rings are free */
 for(uint32_t left=4u*count;!e&&left;){unsigned k=left<BUF?left:BUF;if(f_read(&w->sk,buf,k,&got)!=FR_OK||got!=k){e=1;break;}sh=fnv(sh,buf,k);if(wr(&w->out,buf,k))e=1;left-=k;}
 f_close(&w->sk);
 if(!e){memset(buf,0,512);unsigned pad=audio-(seek_at+4u*count);if(pad&&wr(&w->out,buf,pad))e=1;}
 /* Blocks from NAME.blk, checked against the hash taken while encoding. */
 uint32_t bh=2166136261u;
 if(!e&&f_open(&w->blk,w->path[0],FA_READ)!=FR_OK)e=1;
 for(uint32_t left=1024u*count;!e&&left;){unsigned k=left<BUF?left:BUF;if(f_read(&w->blk,buf,k,&got)!=FR_OK||got!=k){e=1;break;}bh=fnv(bh,buf,k);if(wr(&w->out,buf,k))e=1;left-=k;
  if(poll&&poll(ctx,total,total)){f_close(&w->blk);cleanup(w);return CONVERT_CANCELLED;}}
 f_close(&w->blk);
 if(e){cleanup(w);return CONVERT_WRITE;}
 if(bh!=w->blk_hash){cleanup(w);return CONVERT_VERIFY;}
 le32(h+60,sh);
 if(f_lseek(&w->out,0)!=FR_OK||wr(&w->out,h,64)||f_sync(&w->out)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 /* Read the finished file back: header, metadata, cover, seek table and blocks. */
 uint32_t vh=2166136261u,vs=2166136261u,vb=2166136261u;
 if(f_lseek(&w->out,0)!=FR_OK){cleanup(w);return CONVERT_VERIFY;}
 for(uint32_t pos=0,size=audio+1024u*count;pos<size;){unsigned k=size-pos<BUF?size-pos:BUF;
  if(f_read(&w->out,buf,k,&got)!=FR_OK||got!=k){cleanup(w);return CONVERT_VERIFY;}
  for(unsigned i=0;i<k;i++,pos++){uint8_t c=buf[i];
   if(pos<576)vh=(vh^c)*16777619u;else if(pos>=seek_at&&pos<seek_at+4u*count)vs=(vs^c)*16777619u;else if(pos>=audio)vb=(vb^c)*16777619u;}}
 if(vh!=fnv(2166136261u,h,576)||vs!=sh||vb!=w->blk_hash||f_size(&w->out)!=audio+1024u*count){cleanup(w);return CONVERT_VERIFY;}
 if(f_close(&w->out)!=FR_OK){cleanup(w);return CONVERT_WRITE;}
 if(f_rename(w->path[2],dst)!=FR_OK){cleanup(w);return CONVERT_EXISTS;}
 f_unlink(w->path[0]);f_unlink(w->path[1]);
 return CONVERT_OK;
}
