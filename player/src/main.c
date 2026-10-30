/* Halim Jarrar, 2026. Player logic. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). No GSM/SDK runtime code linked. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mp3dec.h"
#include "storage.h"
#include "mp3_header.h"
#include "player_api.h"
#include "queue.h"
#include "state.h"
#include "library.h"
#include "ezfo_driver.h"
#include "playlist.h"
#include "tags.h"
#include "convert.h"
#include "gac_enc.h"
#include "folder.h"
#include "meta.h"
#include "overlay.h"
#include "alloc_hint.h"
#define BOOST_DEFINE
#include "boost.h"
#define R16(a) (*(volatile uint16_t*)(a))
#define R32(a) (*(volatile uint32_t*)(a))
#define FAST __attribute__((section(".iwram"),noinline))
#define RING 16384u
#define MASK (RING-1u)
/* Keep the ring nearly full: 13824 samples are 313 ms at 44151 Hz (GAC) and
 * 0.84 s at 16384 Hz (MP3). One more decode step (at most one 2304-frame GAC
 * block or one resampled MP3 frame) still fits. v0.8.0 stopped filling at
 * 6144 samples, only 139 ms of 44 kHz GAC, so SD work in the UI underran. */
#define FILL (RING-2560u)
#define SCROLL_MIN_BUFFER 4096u   /* v1.6: staged list moves need this much audio buffered (1/8 s at 32768 Hz) */
#define RATE player_output_rate(stats.output_mode)
#define RATE_DEN player_output_den(stats.output_mode)
#include "player.h"
#include "gfx.h"
#include "cover.h"
static unsigned le32p(const unsigned char *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
volatile struct PlayerStats stats;
/* Helix allocates 23848 bytes (8-byte rounded); the converter's MP3 decoder
 * uses the same arena. 24 KB since v2.2 (32 KB before). */
static unsigned char arena[24576] __attribute__((aligned(8)));
static unsigned arena_used;
static short pcm[2304] __attribute__((aligned(4)));
static int8_t ringL[RING] __attribute__((aligned(4))),ringR[RING] __attribute__((aligned(4)));
static uint8_t ringTrack[RING];
static volatile unsigned wr,rd,play;
static volatile unsigned timer_phase,period_acc;
static unsigned producer_track,producer_tag,frame_index,track_produced;
static volatile unsigned play_tag;
static unsigned char *input;
static int bytes_left;
static HMP3Decoder decoder;
static unsigned step,scale,phase,has_prev;
static int prevL,prevR;
static volatile unsigned key_previous,key_latched;
static unsigned have_audio,saved_t0,saved_t1;
static volatile unsigned transition_end,transition_done;
static unsigned transition_track,cache_skip;
static int gac_el,gac_er;
static unsigned gac_left;   /* frames of the current GAC block not yet decoded */
static int boost_gain=256;  /* Volume Boost gain (Q8), 256 = 0 dB: the unchanged path */
/* MP3 sample to 8 bits with the Volume Boost (one call site keeps resample small). */
extern unsigned __iwram_end;
unsigned player_div64(uint64_t,unsigned);
void *helix_malloc(int n){unsigned k=((unsigned)n+7)&~7u;if(n<=0||k>sizeof(arena)-arena_used)return 0;void*p=arena+arena_used;arena_used+=k;if(arena_used>stats.arena_peak)stats.arena_peak=arena_used;return p;}
void helix_free(void*p){(void)p;}
static FAST unsigned ticks(void){unsigned h,l,h2;do{h=R16(0x400010c);l=R16(0x4000108);h2=R16(0x400010c);}while(h!=h2);return(h<<16)|l;}
static FAST unsigned period(void){return stats.clock_base;}
static FAST __attribute__((optimize("O2"))) void feed16(void){
 unsigned count=wr-rd;if(count<stats.min_queue)stats.min_queue=count;
 if(count<16||count>RING){
  /* Fail closed: never manufacture silence or consume unwritten content. */
  R16(0x4000208)=0;R16(0x4000102)=0;R16(0x4000106)=0;R16(0x4000200)=0;R16(0x4000082)=0x000c;
  stats.underruns++;stats.fatal=2;stats.paused=1;return;
 }
 /* Natural FIFO blocks are word-aligned. memcpy preserves C alias rules;
    the slow path also supports unaligned diagnostic cursors and ring wrap. */
 if(!(rd&3u)){
  for(unsigned j=0;j<16;j+=4){unsigned p=(rd+j)&MASK;uint32_t l,r;
   __builtin_memcpy(&l,__builtin_assume_aligned(ringL+p,4),4);
   __builtin_memcpy(&r,__builtin_assume_aligned(ringR+p,4),4);
   R32(0x40000a0)=l;R32(0x40000a4)=r;}
 }else for(unsigned j=0;j<16;j+=4){unsigned l=0,r=0;for(unsigned k=0;k<4;k++){unsigned p=(rd+j+k)&MASK;l|=(unsigned)(uint8_t)ringL[p]<<(8*k);r|=(unsigned)(uint8_t)ringR[p]<<(8*k);}R32(0x40000a0)=l;R32(0x40000a4)=r;}
 rd+=16;stats.consumed=rd;
}
static FAST unsigned poll_keys(void){
 unsigned keys=(~R16(0x4000130))&1023;
 key_latched|=keys&~key_previous;key_previous=keys;return keys;
}
void FAST player_poll_io(void){
 unsigned ime=R16(0x4000208);R16(0x4000208)=0;poll_keys();R16(0x4000208)=ime;
}
void FAST sound_irq(void){
 unsigned start=ticks();poll_keys();R16(0x4000202)=16;R16(0x3007ff8)|=16;
 R16(0x4000100)=(uint16_t)-period();
 /* IRQ owns these cursors while main is suspended; publish once per block. */
 unsigned p=play,current=stats.current_track,samples=stats.track_samples,changes=stats.transitions;
 /* Every accepted MP3 track contributes >=98 output samples even at the
    smallest supported frame duration; no two track boundaries fit this block.
    Fast common case preserves the exact boundary loop below. */
 if(ringTrack[p&MASK]==play_tag && ringTrack[(p+15)&MASK]==play_tag){p+=16;samples+=16;}
 else for(unsigned i=0;i<16;i++){unsigned tag=ringTrack[p&MASK];if(tag!=play_tag){play_tag=tag;current=tag&63u;samples=0;changes++;}p++;samples++;}
 play=p;stats.current_track=current;stats.track_samples=samples;stats.transitions=changes;
 stats.output_samples+=16;
 if(transition_end&&play>=transition_end){
  R16(0x4000102)=0;R16(0x4000106)=0;R16(0x4000200)=0;R16(0x4000082)=0x000c;
  transition_done=1;stats.paused=1;
 }else{feed16();
  /* v1.7: a finished picture is shown by switching mode-4 pages during the
   * vertical blank (one register write). */
  /* Not while an EZ-Flash Omega maps its kernel over the ROM (gfx_flip_irq
   * is in ROM): the next interrupt, a few milliseconds later, switches. */
  if(gfx_flip_state==1&&!ezfo_busy)gfx_flip_irq();
 }
 stats.irq_cycles+=ticks()-start;
}
static void audio_stop(void){
 if(stats.paused)return;R16(0x4000208)=0;
 /* Finish the current FIFO word before stopping. Sound continues normally
    during this bounded wait; muting first prevents FIFO advancement in mGBA.
    Read timer0 first. */
 unsigned budget=8192,limit=0xff00u;
 while((R16(0x4000100)>limit||(R16(0x4000104)&3u))&&--budget){}
 R16(0x4000102)=0;R16(0x4000106)=0;
 if(!budget)stats.fatal=3;
 saved_t0=R16(0x4000100);saved_t1=R16(0x4000104);
 if(R16(0x4000202)&16)sound_irq();
 R16(0x4000200)=0;R16(0x4000080)=0;R16(0x4000082)=0x000c;stats.paused=1;
}
static void audio_start(void){
 if(stats.fatal||wr-play<32)return;R16(0x4000208)=0;
 if(have_audio){
  /* Preserve zero PSG controls; flush pending emulator mix before routing. */
  R16(0x4000080)=0;R16(0x4000082)=0x120c;R16(0x4000202)=16;R16(0x4000200)=16;
  R16(0x4000104)=saved_t1;R16(0x4000106)=0xc4;R16(0x4000104)=(uint16_t)-16;
  R16(0x4000100)=saved_t0;R16(0x4000102)=0x80;R16(0x4000100)=(uint16_t)-period();
  stats.paused=0;R16(0x4000208)=1;return;
 }
 rd=play;timer_phase=period_acc=0;
 R16(0x4000084)=0x80;R16(0x4000080)=0;R16(0x4000082)=0x9a0c;
 feed16();
 /* Keep12 samples of IRQ latency slack. Seven FIFO words avoid the pinned
  * emulator's full/empty wrap ambiguity; actual FIFO capacity is32 bytes. */
/* v1.1: MP3 at 32768 Hz has GAC's sample period and gets the same slack. */
 if(!stats.fatal){for(unsigned j=0;j<12;j+=4){unsigned p=(rd+j)&MASK;uint32_t l,r;__builtin_memcpy(&l,ringL+p,4);__builtin_memcpy(&r,ringR+p,4);R32(0x40000a0)=l;R32(0x40000a4)=r;}rd+=12;stats.consumed=rd;}
 if(stats.fatal)return;R32(0x3007ffc)=(uintptr_t)sound_irq;R16(0x4000202)=16;R16(0x4000200)=16;
 R16(0x4000104)=(uint16_t)-16;R16(0x4000106)=0xc4;R16(0x4000100)=(uint16_t)-period();
 R16(0x4000102)=0x80;
 have_audio=1;stats.paused=0;R16(0x4000208)=1;
 /* Startup keeps IRQ/FIQ masked until callback and sound are initialized. */
 unsigned status;__asm__ volatile("mrs %0,cpsr\n\tbic %0,%0,#0x80\n\tmsr cpsr_c,%0":"=&r"(status)::"memory","cc");
}
static void append(int l,int r){if(stats.fatal)return;if(wr-rd>=RING){stats.fatal=3;return;}unsigned p=wr&MASK;
 /* MP3 endpoint holds and GAC ring values are both already boosted (v1.1). */
 ringL[p]=l>>8;ringR[p]=stats.output_mode?ringL[p]:r>>8;ringTrack[p]=(uint8_t)producer_tag;__asm__ volatile("":::"memory");wr++;track_produced++;stats.produced=wr;if(wr-rd>stats.max_queue)stats.max_queue=wr-rd;}
/* v1.1: linear interpolation from the decoder rate to 32768 Hz. The output
 * rate makes scale = 32768 << k, so the 14-bit weight is a shift of the
 * phase (no multiply); Volume Boost runs once per decoded sample before the
 * interpolation (not per output sample), and prevL/prevR hold boosted PCM.
 * tests/check_experimental_dac.py mirrors this arithmetic. */
static FAST __attribute__((optimize("Os"))) void resample(const MP3FrameInfo*f){
 if(stats.fatal)return;
 if((f->nChans!=1&&f->nChans!=2)||f->outputSamps<=0||f->outputSamps>1152*f->nChans||(f->outputSamps&(f->nChans-1))){stats.fatal=3;return;}
 if(step<8000||step>144000||scale<32768||scale>262144||(scale&(scale-1))||phase>step){stats.fatal=3;return;}
 unsigned used=wr-rd,n=(unsigned)f->outputSamps>>(f->nChans==2);
 /* IRQ can only increase free space after this conservative reservation. */
 if(used>=RING||n*scale>(RING-used-1)*step){stats.fatal=3;return;}
 unsigned w=wr,w0=w,ph=phase,st=step,sc=scale,sh=1;
 unsigned tag=producer_tag,have=has_prev;int pl=prevL,pr=prevR;const int g=boost_gain;
 while((32768u<<(sh-1))<sc)sh++;   /* scale = 32768<<(sh-1): weight = ph>>sh, 0..16384 */
 const short *in=pcm;
 if(f->nChans==1){
  for(unsigned i=0;i<n;i++){
   int l=in[i];if(g!=256)l=boost_apply(l,g);
   if(!have){unsigned at=w++&MASK;int v=l>>8;ringL[at]=v;ringR[at]=v;ringTrack[at]=tag;ph=st;have=1;pl=l;continue;}
   int dl=l-pl;
   while(ph<=sc){unsigned at=w++&MASK;int v=(pl+((dl*(int)(ph>>sh)+8192)>>14))>>8;ringL[at]=v;ringR[at]=v;ringTrack[at]=tag;ph+=st;}
   ph-=sc;pl=l;
  }
  pr=pl;
 }else for(unsigned i=0;i<n;i++){
  int l=in[2*i],r=in[2*i+1];if(g!=256){l=boost_apply(l,g);r=boost_apply(r,g);}
  if(!have){unsigned at=w++&MASK;ringL[at]=l>>8;ringR[at]=r>>8;ringTrack[at]=tag;ph=st;have=1;pl=l;pr=r;continue;}
  int dl=l-pl,dr=r-pr;
  while(ph<=sc){unsigned at=w++&MASK,k=ph>>sh;ringL[at]=(pl+((dl*(int)k+8192)>>14))>>8;ringR[at]=(pr+((dr*(int)k+8192)>>14))>>8;ringTrack[at]=tag;ph+=st;}
  ph-=sc;pl=l;pr=r;
 }
 phase=ph;prevL=pl;prevR=pr;has_prev=have;track_produced+=w-w0;
 __asm__ volatile("":::"memory");wr=w;stats.produced=w;if(w-rd>stats.max_queue)stats.max_queue=w-rd;
}
/* ---- v0.8.0 player core: queue-driven tracks, per-tag track info ---- */
#include "input.h"
static struct NowPlaying slots[4],idle_now;
static unsigned serial,started,ended,pending_end,state_dirty,state_last;
static char path_buf[STORAGE_PATH],pending_path[STORAGE_PATH];
static struct AppState app;
static FIL metaf;
static const char *base_name(const char *p){const char *b=p;for(;*p;p++)if(*p=='/')b=p+1;return b;}
static void load_meta(const char *path,struct NowPlaying *n){
 n->valid=0;unsigned k=strlen(path);if(k>=STORAGE_PATH)return;
 memcpy(n->path,path,k+1);n->key=storage_key(path);
 if(f_open(&metaf,path,FA_READ)!=FR_OK){memset(&n->meta,0,sizeof(n->meta));return;}
 meta_read(&metaf,base_name(path),&n->meta);
 f_close(&metaf);n->valid=1;
 /* v0.9.8: album covers come from the library (v2.1: GAC songs' too,
  * reduced to their colours at Update Library). */
 uint32_t id;
 if(!storage_cover(n->key)&&!library_find(path,&id)){
  volatile unsigned short *c=storage_cover_slot(n->key);if(!library_cover(id,c))storage_cover_ready(n->key);
 }
}
static void remember(const char *path){
 unsigned k=strlen(path);if(k<STORAGE_PATH){memcpy(app.last_song,path,k+1);app.has_song=1;}
 memcpy(&app.queue,queue_source(),sizeof(app.queue));app.queue_index=queue_index();state_dirty=1;
}
/* v0.9.8 Part B2a/B2c: Mono gets 8 bands (7 at 48 kHz) instead of 6 unless
 * the song is constant-bitrate above 256 kbps (the heaviest files keep the
 * v0.9.5 settings; VBR and unknown bitrates get 8 bands, measured within
 * the v0.9.5 worst case even at 320 kbps). tests/check_experimental_dac.py
 * mp3_mono_hq() mirrors this. */
static unsigned mp3_mono_hq(const struct SongMeta *m){return m->rate>=32000&&!(m->bitrate>256&&!m->vbr);}
static void begin_track(const char *path){
 serial=(serial+1)&63u;producer_track=serial;producer_tag=((producer_tag^64u)&64u)|serial;frame_index=track_produced=0;arena_used=0;
 load_meta(path,&slots[serial&3u]);remember(path);
 int opened=storage_open(path);if(opened){stats.fatal=opened==-4?206:opened==-3?203:201;return;}has_prev=phase=0;
 stats.cache_audio=storage_is_cache();cache_skip=0;gac_el=gac_er=0;gac_left=0;
 stats.cache_rate=stats.cache_audio?storage_cache_info()->rate:0;
 /* v0.8.0: one Stereo/Mono setting for both formats. */
 stats.cache_sound_mode=stats.sound_mode;stats.output_mode=player_sound_mono(stats.sound_mode);
 stats.clock_base=stats.cache_audio?player_cache_period(stats.cache_rate):player_output_period(stats.output_mode);
 /* 65.5 kHz PWM for 44151 Hz GAC. */
/* v1.1: MP3 (32768 Hz) and 32768 Hz GAC: one sample per 32.8 kHz PWM cycle. */
 R16(0x4000088)=stats.cache_rate>32768?0x4200:0x200;
 /* Swaps happen only here: GAC transitions stop audio first (pump), and
  * MP3 -> MP3 keeps the loaded overlay, so gapless playback never copies. */
 if(stats.cache_audio){decoder_use(DECODER_GAC);return;}
 decoder_use(DECODER_MP3);
 /* v0.9.8: Mono keeps 8 bands instead of 6 (7 at 48 kHz) when the song's
  * bitrate leaves the CPU for it (mp3_quality). */
 stats.mp3_quality=stats.output_mode?1u+mp3_mono_hq(&slots[serial&3u].meta):0u;
 decoder=MP3InitDecoder();if(!decoder||MP3SetOutputMode(decoder,(int)stats.mp3_quality)){stats.fatal=1;return;}
}
/* GAC v4 decodes straight into the 8-bit ring with per-channel noise
 * shaping, at most 16 units (1024 frames) per step so a block of up to 8192
 * frames never needs more ring space than the FILL margin. */
#define GAC_STEP_UNITS 16u
static void cache_output(void){
 unsigned w=wr;
 if(w-rd+GAC_STEP_UNITS*GAC_UNIT>RING){stats.fatal=3;return;}
 struct GacOut o={ringL,ringR,ringTrack,w,MASK,producer_tag,stats.output_mode,gac_el,gac_er,boost_gain};
 unsigned skip=gac4.skip,t=ticks(),n=gac4_decode(GAC_STEP_UNITS,gac_left,&o);stats.conversion_cycles+=ticks()-t;
 unsigned used=n+(skip-gac4.skip);gac_left-=used<gac_left?used:gac_left;
 w=o.w;gac_el=o.el;gac_er=o.er;
 if(w!=wr){prevL=ringL[(w-1)&MASK]*256;prevR=ringR[(w-1)&MASK]*256;has_prev=1;}
 track_produced+=n;
 __asm__ volatile("":::"memory");wr=w;stats.produced=w;if(w-rd>stats.max_queue)stats.max_queue=w-rd;
}
static int decode_frame(int discard){
 /* Never jump into the wrong overlay: a visible error instead of a crash. */
 if(stats.decoder!=(stats.cache_audio?DECODER_GAC:DECODER_MP3)){stats.fatal=205;return 0;}
 if(stats.cache_audio){
  unsigned start=ticks();
  if(!gac_left){
   unsigned n;int got=storage_cache_next(&input,&n);
   if(got<0){stats.fatal=got==-3?203:201;return 0;}if(!got)return 0;
   memcpy(gac4.block,input,GAC_BLOCK);gac4_begin(storage_cache_info()->channels,cache_skip<n?cache_skip:n);
   gac_left=n;cache_skip=0;frame_index++;
  }
  (void)discard;cache_output();
  stats.decode_cycles+=ticks()-start;stats.decoded_frames++;
  player_poll_io();return 1;
 }
 unsigned n;int got=storage_next(&input,&n);
 if(got<0){stats.fatal=got==-3?203:201;return 0;}if(!got)return 0;
 bytes_left=n;unsigned start=ticks();int e=MP3Decode(decoder,&input,&bytes_left,pcm,0);
 stats.decode_cycles+=ticks()-start;frame_index++;stats.decoded_frames++;
 if(e){if(discard&&e==-2)return 1;stats.fatal=(unsigned)(100-e);return 0;}
 if(bytes_left){stats.fatal=203;return 0;}
 MP3FrameInfo f;MP3GetLastFrameInfo(decoder,&f);step=f.samprate*RATE_DEN;scale=RATE*f.outputRateDen;
 if(!discard){start=ticks();resample(&f);stats.conversion_cycles+=ticks()-start;}player_poll_io();return 1;
}
/* Stop on a FIFO block boundary; the IRQ halts there and main continues. */
static void stop_at_block(void){
 while(wr&15)append(prevL,prevR);
 unsigned end=wr;for(unsigned j=0;j<16;j++)append(prevL,prevR);
 transition_end=end;
}
static void pump(void){
 input_poll((uint16_t)(~R16(0x4000130)&0x3ff));   /* long prefills must not swallow taps */
 if(stats.fatal||transition_end)return;
 if(!decode_frame(0)&&!stats.fatal){
  if(!has_prev){stats.fatal=203;return;}
  /* Endpoint hold to the rational frame-duration boundary, no gap/silence cache. */
  unsigned target=stats.cache_audio?track_produced:player_div64((uint64_t)frame_index*(step>24000*RATE_DEN?1152:576)*RATE+step/2,step);
  if(target<track_produced||target-track_produced>16){stats.fatal=203;return;}
  while(track_produced<target)append(prevL,prevR);
  if(!queue_next(0,path_buf)){pending_end=1;stop_at_block();return;}
  if(stats.cache_audio||storage_path_is_cache(path_buf)){memcpy(pending_path,path_buf,STORAGE_PATH);stop_at_block();}
  else begin_track(path_buf);
 }
}
static unsigned exact_frame=~0u;   /* GAC: restart at this frame instead of a second */
static void change_track(const char *path,unsigned target_seconds){
 unsigned paused=stats.paused;audio_stop();have_audio=0;wr=rd=play=0;stats.produced=stats.consumed=0;
 transition_end=transition_done=pending_end=0;
 begin_track(path);started=1;ended=0;
 unsigned exact=exact_frame;exact_frame=~0u;
 if(exact!=~0u&&stats.cache_audio&&!stats.fatal){
  unsigned sample;
  if(storage_cache_seek_frame(exact,&sample))stats.fatal=204;
  else{unsigned total=storage_cache_info()->total,target=exact<total?exact:total-1;cache_skip=target-sample;track_produced=target;frame_index=0;}
 }else if(target_seconds&&!stats.fatal){
  if(stats.cache_audio){
   unsigned sample;
   if(storage_cache_seek(target_seconds,&sample))stats.fatal=204;
   else{unsigned total=storage_cache_info()->total;
    unsigned target=target_seconds>(total-1)/stats.cache_rate?total-1:target_seconds*stats.cache_rate;
    cache_skip=target-sample;track_produced=target;frame_index=0;
   }
  }else{
  unsigned warm,target;struct MP3Header h;
  if(storage_seek_seconds(target_seconds,&warm,&target,&h))stats.fatal=204;
  else{frame_index=warm;while(frame_index<target&&!stats.fatal)decode_frame(1);
   track_produced=player_div64((uint64_t)frame_index*h.samples*RATE+(h.rate*RATE_DEN)/2,h.rate*RATE_DEN);}}
 }
 stats.generation++;stats.anchor_frame=stats.cache_audio?track_produced:frame_index;stats.anchor_track=producer_track;stats.anchor_pos=0;
 stats.anchor_boost=stats.boost;stats.boost_changes=0;
 play_tag=producer_tag;stats.current_track=producer_track;stats.track_samples=track_produced;has_prev=phase=0;
 /* Audio starts after 4096 samples (0.125 s at 32768 Hz, v1.1) so the UI stays
  * responsive; the loop keeps filling. */
 while(wr<4096u&&!stats.fatal&&!transition_end)pump();if(!paused&&!stats.fatal)audio_start();stats.controls++;
}
static struct QueueSource tmp_src;   /* scratch: keeps large sources off the stack */
/* ---- player API ---- */
static unsigned elapsed_now(void){return stats.cache_audio&&stats.cache_rate?stats.track_samples/stats.cache_rate:player_output_seconds(stats.track_samples,stats.output_mode);}
const struct NowPlaying *player_now(void){return started&&!ended?&slots[stats.current_track&3u]:&idle_now;}
unsigned player_elapsed(void){return started&&!ended?elapsed_now():0;}
/* Between two songs the sound interrupt has paused at the block boundary
 * (transition_done) and the main loop starts the next song on its next pass:
 * that is still playing (the lock screen must not put the GBA to sleep). */
unsigned player_is_playing(void){return started&&!ended&&(!stats.paused||(transition_done&&!pending_end));}
unsigned player_loaded(void){return (started&&!ended)||idle_now.valid;}
unsigned player_active(void){return started&&!ended;}
unsigned player_fatal(void){return stats.fatal;}
const char *player_error_text(void){
 switch(stats.fatal){case 0:return "";case 200:return "SD not ready";case 2:return "Audio underrun";case 201:return "SD read error";case 204:return "Seek limit";case 205:return "Decoder not loaded";case 206:return "OLD GAC - RECONVERT";default:return "Audio file error";}
}
static void clear_error(void){if(stats.fatal&&stats.fatal!=200){stats.fatal=0;have_audio=0;}}
static void idle_from(const char *path){load_meta(path,&idle_now);}
void player_play(const struct QueueSource *src,uint32_t index){
 clear_error();queue_set(src,index);
 if(queue_current(path_buf)){return;}
 audio_stop();change_track(path_buf,0);if(!stats.fatal)audio_start();
}
void player_pause(void){if(started&&!ended)audio_stop();}
void player_toggle(void){
 clear_error();
 if(!started||ended){
  int ok=ended?queue_restart(path_buf):!queue_current(path_buf);
  if(!ok&&idle_now.valid){struct QueueSource *s=&tmp_src;memset(s,0,sizeof(*s));s->kind=QUEUE_SINGLE;memcpy(s->text,idle_now.path,STORAGE_PATH);queue_set(s,0);memcpy(path_buf,idle_now.path,STORAGE_PATH);ok=1;}
  if(!ok)return;
  stats.paused=1;change_track(path_buf,0);if(!stats.fatal)audio_start();return;
 }
 if(stats.paused)audio_start();else audio_stop();
}
void player_next(void){
 clear_error();if(!queue_count())return;
 if(queue_next(1,path_buf)){if(!started||ended){idle_from(path_buf);started=0;ended=0;return;}change_track(path_buf,0);}
}
void player_prev(void){
 clear_error();if(!queue_count())return;
 if(started&&!ended&&elapsed_now()>3){queue_current(path_buf);change_track(path_buf,0);return;}
 if(!queue_prev(path_buf))queue_current(path_buf);
 if(!started||ended){idle_from(path_buf);started=0;ended=0;return;}
 change_track(path_buf,0);
}
void player_seek(int delta){
 if(!started||ended||stats.fatal)return;
 int s=(int)elapsed_now()+delta;if(s<0)s=0;
 const struct SongMeta *m=&slots[stats.current_track&3u].meta;
 if(delta>0&&m->duration_ms&&(unsigned)s*1000u>=m->duration_ms){player_next();return;}
 memcpy(path_buf,slots[stats.current_track&3u].path,STORAGE_PATH);
 change_track(path_buf,(unsigned)s);stats.seeks++;
}
unsigned player_mode(void){return queue_mode;}
void player_set_mode(unsigned m){queue_set_mode(m);app.mode=(uint8_t)queue_mode;stats.browser_order=queue_mode;state_dirty=1;}
unsigned player_stereo(void){return stats.sound_mode==3;}
void player_set_stereo(unsigned stereo){
 unsigned mode=stereo?3u:1u;if(mode==stats.sound_mode)return;
 app.stereo=stereo!=0;state_dirty=1;
 /* v1.8: the silence between a song and a GAC (the queue has moved on to
  * it) is not a pause: switching there left playback stopped for good. The
  * waiting song starts now, in the new mode. */
 unsigned between=transition_end&&!pending_end;
 unsigned paused=stats.paused&&!between;audio_stop();
 unsigned seconds=elapsed_now();
 stats.sound_mode=mode;stats.cache_sound_mode=mode;stats.output_mode=player_sound_mono(mode);
 stats.clock_base=stats.cache_audio?player_cache_period(stats.cache_rate):player_output_period(stats.output_mode);
 if(started&&!ended&&!stats.fatal){
  if(between){memcpy(path_buf,pending_path,STORAGE_PATH);change_track(path_buf,0);stats.transitions++;}
  else{memcpy(path_buf,slots[stats.current_track&3u].path,STORAGE_PATH);change_track(path_buf,seconds);}}
 stats.mode_changes++;
 if(!paused&&started&&!stats.fatal)audio_start();
}
/* Volume Boost. The level is applied where samples become 8-bit, before the
 * ring buffer, to newly decoded audio (MP3 and, v2.2, GAC alike): what is
 * buffered plays out first, so a change is heard within about 0.3 s (GAC at
 * 44151 Hz; 0.42 s at 32768 Hz and for MP3). v0.9.5-v2.1 restarted GAC
 * decoding at the playing sample, which could underrun deep into a long
 * file and froze for 0.45 s per step while paused. The noise shaper state
 * carries on; stats.boost_at logs where each change starts (check_ring). */
static void boost_apply_level(void){
 unsigned level=app.boost_on?app.boost_level:0u;int g=boost_gains[level&3];
 if(g==boost_gain)return;
 boost_gain=g;stats.boost=level;
 unsigned k=stats.boost_changes++;if(k<BOOST_LOG)stats.boost_at[k]=wr<<2|level;
}
unsigned player_boost_on(void){return app.boost_on;}
int player_scroll_ok(void){return stats.paused||wr-rd>=SCROLL_MIN_BUFFER;}
int player_read_ahead_ok(void){return stats.paused||wr-rd>=2u*SCROLL_MIN_BUFFER;}   /* 1/4 s */
unsigned player_boost_level(void){return app.boost_on?app.boost_level:0u;}
void player_set_boost_on(unsigned on){
 on=on!=0;if(on==app.boost_on)return;
 app.boost_on=(uint8_t)on;app.boost_level=0;state_dirty=1;boost_apply_level();
}
void player_boost_step(int dir){
 if(!app.boost_on)return;
 int l=(int)app.boost_level+dir;if(l<0||l>3)return;
 app.boost_level=(uint8_t)l;state_dirty=1;boost_apply_level();
}
unsigned player_search_field(void){return app.search_field;}
void player_set_search_field(unsigned f){app.search_field=(uint8_t)(f&3u);state_dirty=1;}
const struct MenuOrder *player_menus(void){return &app.menus;}
void player_menu_toggle(void){app.menus.enabled^=MENU_ORDER_ON;state_dirty=1;}
unsigned player_auto_update(void){return app.auto_update;}
/* v1.8 Secret Settings. Show Songs takes effect in Folders at once and in
 * the library with the next Update Library (Secret Settings runs it). */
unsigned player_song_types(void){return app.song_types;}
void player_song_types_next(void){app.song_types=(uint8_t)((app.song_types+1)%3);folder_song_mask=app.song_types==1?1u:app.song_types==2?2u:3u;folder_sort_epoch++;state_dirty=1;}
unsigned player_new_playlist_top(void){return app.new_playlist_top&1u;}
void player_set_new_playlist_top(unsigned top){app.new_playlist_top=(uint8_t)((app.new_playlist_top&~1u)|(top!=0));state_dirty=1;}
/* 11.0: "+ Add Songs" on top of every playlist (bit 2 of new_playlist_top). */
unsigned player_add_songs_top(void){return app.new_playlist_top>>2&1u;}
void player_set_add_songs_top(unsigned top){app.new_playlist_top=(uint8_t)((app.new_playlist_top&~4u)|(top?4u:0u));state_dirty=1;}
/* Letter Box: 0 Off (default), 1 On, 2 only with L + Up / Down (flags bits 3-4;
 * bit 0, the v1.8 Off switch, is no longer used). */
unsigned player_letter_box(void){return (app.flags>>3)&3;}
void player_letter_box_toggle(void){unsigned m=(player_letter_box()+1)%3;app.flags=(uint8_t)((app.flags&~0x18u)|(m<<3));state_dirty=1;}
/* v1.9 Secret Settings: Rename files / Delete files (Song, folder and
 * collection options). */
unsigned player_file_ops(void){return (app.flags>>1)&3;}
/* v2.0 Secret Settings > Conversion: 0 Off, 1 MP3>GAC, 2 GAC>MP3 (flags bits 5-6). */
unsigned player_conversion(void){return (app.flags>>5)&3;}
void player_conversion_toggle(void){unsigned m=(player_conversion()+1)%3;app.flags=(uint8_t)((app.flags&~0x60u)|(m<<5));state_dirty=1;}
unsigned player_ticks(void){return ticks();}
void player_file_ops_toggle(unsigned bit){app.flags^=(uint8_t)(bit<<1);state_dirty=1;}
/* v2.2 Secret Settings > Hold START to unlock, On by default: bit 1 of
 * new_playlist_top set = Off (one short START unlocks). */
unsigned player_hold_unlock(void){return !(app.new_playlist_top&2u);}
void player_hold_unlock_toggle(void){app.new_playlist_top^=2u;state_dirty=1;}
static struct DirCursor queue_dir;static int resolve(const struct QueueSource *src,uint32_t i,char *out);
/* v1.9: renaming or deleting files first stops playback and forgets the
 * current song and its list (no file of the card stays open); the Play
 * Next list stays (missing files are skipped when they come up). */
void player_stop_all(void){
 audio_stop();started=ended=0;idle_now.valid=0;have_audio=0;
 transition_done=pending_end=transition_end=0;storage_close();
 struct QueueSource *q=&tmp_src;memset(q,0,sizeof(*q));
 unsigned n=queue_overlay_count();queue_set(q,0);queue_overlay_import(n,0);
 app.has_song=0;app.last_song[0]=0;app.queue=*q;app.queue_index=0;state_dirty=1;
}
/* Path `p` under `old` (the file itself or, dir, inside the folder): the
 * same path under `new` in out. */
static int moved(const char *p,const char *old,const char *new,unsigned dir,char *out){
 unsigned a=strlen(old);
 if(memcmp(p,old,a)||(p[a]&&!(dir&&p[a]=='/')))return 0;
 unsigned b=strlen(new),c=strlen(p+a);if(b+c>=STORAGE_PATH)return 0;
 memmove(out+b,p+a,c+1);memcpy(out,new,b);return 1;
}
/* v2.2 exFAT: the song's folder is repaired first (a power cut can leave a
 * torn entry set there), and once more on FR_INT_ERR. Writes enabled. */
static FRESULT del(const char *path){
 (void)library_repair_folder_of(path);FRESULT r=f_unlink(path);
 if(r==FR_INT_ERR&&library_repair_folder_of(path)>0)r=f_unlink(path);
 return r;
}
/* Play Next and playlists follow a new name (-2: playlists not updated). */
static int follow_paths(const char *old,const char *new,unsigned dir){
 static char t[STORAGE_PATH];
 for(unsigned i=0;i<queue_overlay_count();i++){char *e=queue_overlay_entry(i);memcpy(t,e,STORAGE_PATH);if(moved(t,old,new,dir,t))memcpy(e,t,STORAGE_PATH);}
 queue_pn_touch();
 return playlist_rename_paths(old,new,dir)?-2:0;
}
int player_rename(const char *old,const char *new,unsigned dir){
 player_stop_all();
 {extern void disk_index_writes(unsigned);disk_index_writes(1);(void)library_repair_folder_of(old);FRESULT r=f_rename(old,new);
  if(r==FR_INT_ERR&&library_repair_folder_of(old)>0)r=f_rename(old,new);   /* v2.2 exFAT: torn entry set */
  disk_index_writes(0);if(r!=FR_OK)return -1;}
 return follow_paths(old,new,dir);   /* -2: renamed, playlists not updated */
}
/* v2.0 conversion: `old` was converted to `new` (checked): delete `old`;
 * Play Next and playlists follow. 0, -1 (not deleted: both stay), -2 (the
 * playlists not updated). */
int player_replace_original(const char *old,const char *new){
 player_stop_all();
 {extern void disk_index_writes(unsigned);disk_index_writes(1);FRESULT r=del(old);disk_index_writes(0);if(r!=FR_OK)return -1;}
 return follow_paths(old,new,0);
}
int player_tag_set(const char *path,unsigned field,const char *text){
 unsigned n;uint8_t *b=library_scratch(&n);   /* idle: Update Library follows */
 extern void disk_index_writes(unsigned);disk_index_writes(1);int r=tag_set(path,field,text,b,n);disk_index_writes(0);
 player_service();return r;
}
/* Conversion (MP3 -> GAC, v2.0 also GAC -> MP3 by the source's extension): music stops; the library scratch area holds the
 * converter's work memory, the cover and the tags (Update Library follows). */
static convert_poll_fn conv_user;static void *conv_ctx;static unsigned conv_tick;
static int conv_poll(void *ctx,uint32_t done,uint32_t total){(void)ctx;
 unsigned t=ticks();stats.conv_cycles+=t-conv_tick;conv_tick=t;   /* ticks() wraps after 256 s: summed per frame */
 stats.conv_frames=done;stats.decode_cycles=convert_stats.cyc_decode;stats.conversion_cycles=convert_stats.cyc_resample;stats.irq_cycles=convert_stats.cyc_encode;stats.ui_cycles=convert_stats.cyc_lpc;stats.seeks=convert_stats.cyc_sub;stats.controls=convert_stats.cyc_close;stats.output_samples=convert_stats.peak_dec;stats.max_queue=convert_stats.peak_res;
 {extern struct GacEnc *conv_enc_dbg;if(conv_enc_dbg){stats.arena_peak=conv_enc_dbg->cyc_l[0];stats.main_margin=conv_enc_dbg->cyc_l[1];stats.irq_margin=conv_enc_dbg->cyc_l[2];}}   /* conversion telemetry (no music meanwhile) */
 return conv_user?conv_user(conv_ctx,done,total):0;}
static void conv_use(int conversion){decoder_use(conversion?DECODER_CONV:DECODER_MP3);}
/* v2.0 GAC -> MP3: the GAC decoder (its state kept) and the conversion
 * overlay take turns; the whole library scratch area is the converter's. */
static void conv_use_gac(int conversion){if(conversion)decoder_use(DECODER_MENC);else decoder_use_keep(DECODER_GAC);}
static int convert_gac(const char *src,const char *dst,convert_poll_fn poll,void *ctx){
 unsigned n;uint8_t *z=library_scratch(&n);
 conv_user=poll;conv_ctx=ctx;conv_tick=ticks();
 extern void disk_index_writes(unsigned);disk_index_writes(1);(void)library_repair_folder_of(dst);   /* v2.2 exFAT: NAME.part etc. go there */
 int r=convert_gac_to_mp3(src,dst,z,(int16_t*)(void*)ringL,(int16_t*)(void*)ringR,RING/2u,conv_use_gac,conv_poll,0);
 decoder_use(DECODER_MP3);
 disk_index_writes(0);
 stats.conv_cycles+=ticks()-conv_tick;stats.conv_frames=convert_stats.frames_in;stats.conv_result=(uint32_t)r;
 return r;
}
int player_convert(const char *src,const char *dst,convert_poll_fn poll,void *ctx){
 player_stop_all();
 {unsigned k=strlen(src);if(k>4&&(src[k-1]|32)=='c'&&(src[k-2]|32)=='a'&&(src[k-3]|32)=='g'&&src[k-4]=='.')return convert_gac(src,dst,poll,ctx);}
 unsigned n;uint8_t *z=library_scratch(&n);
 uint16_t *art=(uint16_t*)(void*)(z+CONVERT_WORK_BYTES);struct SongMeta *m=(struct SongMeta*)(void*)(z+CONVERT_WORK_BYTES+8192);FIL *f=(FIL*)(void*)(m+1);
 memset(m,0,sizeof(*m));const char *b=src;for(const char *q=src;*q;q++)if(*q=='/')b=q+1;
 if(f_open(f,src,FA_READ)==FR_OK){meta_read(f,b,m);f_close(f);}
 uint32_t id;int has_art=!library_find(src,&id)&&!library_cover(id,art);
 decoder_use(DECODER_MP3);arena_used=0;
 conv_user=poll;conv_ctx=ctx;conv_tick=ticks();
 extern void disk_index_writes(unsigned);disk_index_writes(1);(void)library_repair_folder_of(dst);   /* v2.2 exFAT: NAME.part etc. go there */
 int r=convert_mp3_to_gac(src,dst,m,has_art?art:0,z,(int16_t*)(void*)ringL,(int16_t*)(void*)ringR,RING/2u,conv_use,conv_poll,0);
 decoder_use(DECODER_MP3);
 disk_index_writes(0);
 stats.conv_cycles+=ticks()-conv_tick;stats.conv_frames=convert_stats.frames_out;stats.conv_result=(uint32_t)r;
 return r;
}
int player_delete_file(const char *path){
 player_stop_all();
 extern void disk_index_writes(unsigned);disk_index_writes(1);FRESULT r=del(path);disk_index_writes(0);
 return r==FR_OK?0:-1;
}
/* The songs directly in folder `dir` (as Folders lists them), then the folder
 * itself when nothing else is left in it. *kept: the folder stayed. */
int player_delete_folder(const char *dir,unsigned *n,unsigned *kept){
 player_stop_all();*n=0;*kept=1;
 extern void disk_index_writes(unsigned);disk_index_writes(1);   /* writes allowed until the end */
 (void)library_repair_dir(dir);   /* v2.2 exFAT: the folder and those above it */
 static DIR d;static FILINFO fi;static char p[STORAGE_PATH];int err=0;
 for(unsigned guard=0;guard<65536;guard++){
  if(f_opendir(&d,dir)!=FR_OK){err=-1;break;}
  int found=0;
  while(f_readdir(&d,&fi)==FR_OK&&fi.fname[0]){if(!(fi.fattrib&AM_DIR)&&fi.fname[0]!='.'&&folder_is_song(fi.fname)){found=1;break;}}
  f_closedir(&d);if(!found)break;
  if(!folder_join(p,dir,fi.fname)||f_unlink(p)!=FR_OK){err=-1;break;}
  (*n)++;player_service();
 }
 if(!err&&f_unlink(dir)==FR_OK)*kept=0;   /* fails while other files or folders remain */
 disk_index_writes(0);return err;
}
/* v1.9 Sleep: with nothing playing, the lock screen stops the GBA (BIOS
 * Stop: processor, display, sound and timers halt, about as little power as
 * the GBA's own sleep). START wakes it. The interrupt handler is swapped for
 * a keypad one meanwhile (the sound interrupt is off when paused). */
static void key_irq(void){R16(0x4000202)=0x1000;R16(0x3007ff8)|=0x1000;}
/* v2.0 conversion: no sound interrupt samples the keys meanwhile. Timer 0
 * (free while no music plays) interrupts 60 times a second and latches the
 * keys down, until player_keys_release(): a short press is never missed
 * between two progress updates. */
static volatile unsigned conv_keys;
/* In RAM: conversion reads and writes the card with this interrupt on, and an
 * EZ-Flash Omega maps its kernel over the ROM meanwhile. */
static __attribute__((section(".ewram.conv_key_tick"),target("arm"),noinline)) void conv_key_tick(void){conv_keys|=(~R16(0x4000130))&1023u;R16(0x4000202)=8;R16(0x3007ff8)|=8;}
unsigned player_keys_take(void){
 R16(0x4000208)=0;
 unsigned k=conv_keys|((~R16(0x4000130))&1023u);conv_keys=0;
 /* (re)armed every call: stopping the music in between resets timer 0 */
 R32(0x3007ffc)=(uint32_t)(uintptr_t)conv_key_tick;
 if((R16(0x4000102)&0xc3)!=0xc3){R16(0x4000102)=0;R16(0x4000100)=(uint16_t)-273;R16(0x4000102)=0x00c3;}   /* 16384 Hz / 273 */
 R16(0x4000202)=8;R16(0x4000200)=(uint16_t)(R16(0x4000200)|8u);
 R16(0x4000208)=1;
 /* IRQs are masked in the CPU until music first plays (audio_start). */
 unsigned status;__asm__ volatile("mrs %0,cpsr\n\tbic %0,%0,#0x80\n\tmsr cpsr_c,%0":"=&r"(status)::"memory","cc");
 return k;
}
void player_keys_release(void){
 R16(0x4000208)=0;
 R16(0x4000102)=0;R16(0x4000200)=(uint16_t)(R16(0x4000200)&~8u);R16(0x4000202)=8;conv_keys=0;
 R16(0x4000208)=1;
}
void player_sleep(void){
 if(!stats.paused||stats.fatal)return;
 R16(0x4000208)=0;
 unsigned ie=R16(0x4000200),disp=R16(0x4000000),snd=R16(0x4000084);uint32_t handler=R32(0x3007ffc);
 R16(0x4000000)=(uint16_t)(disp|0x80);R16(0x4000084)=0;           /* display blank, sound circuit off */
 R32(0x3007ffc)=(uint32_t)(uintptr_t)key_irq;R16(0x4000132)=0x4008;  /* keypad interrupt: START */
 R16(0x4000202)=0xffff;R16(0x4000200)=0x1000;
 for(;;){stats.sleeps++;
  R16(0x4000208)=1;
  /* Stop ends on the START interrupt; anything else (an emulator without
   * Stop returns at once) stops again until START is down. */
  do{__asm__ volatile("swi 0x030000":::"r0","r1","r2","r3","memory");}while(R16(0x4000130)&8);
  /* v2.2 Hold START to unlock: START must stay down for 1 s (60 frames of
   * the cycle timer), the screen still off; released earlier: Stop again. */
  if(!player_hold_unlock())break;
  R16(0x4000208)=0;   /* no keypad interrupts while START is held */
  unsigned t=ticks();while(!(R16(0x4000130)&8)&&ticks()-t<60u*280896u){}
  if(!(R16(0x4000130)&8))break;
  R16(0x4000202)=0x1000;
 }
 R16(0x4000208)=0;R16(0x4000132)=0;R16(0x4000202)=0x1000;
 R32(0x3007ffc)=handler;R16(0x4000200)=(uint16_t)ie;R16(0x4000084)=(uint16_t)snd;R16(0x4000000)=(uint16_t)disp;
 R16(0x4000208)=1;
 input_reset((~R16(0x4000130))&1023);   /* START is still held: no stale taps */
}
unsigned player_folder_count(const char *path){folder_bind(&queue_dir,path,FOLDER_SONGS);int n=folder_count(&queue_dir);return n>0?(unsigned)n:0;}
int player_resolve(const struct QueueSource *src,uint32_t i,char *out){
 if(src->kind==QUEUE_SINGLE){unsigned k=strlen(src->text);if(i||k>=STORAGE_PATH)return -1;memcpy(out,src->text,k+1);return 0;}
 return resolve(src,i,out);
}
unsigned player_play_next_free(void){return started&&!ended?QUEUE_OVERLAY-queue_overlay_count():QUEUE_OVERLAY;}
void player_auto_update_toggle(void){app.auto_update=!app.auto_update;state_dirty=1;}
unsigned player_song_files(void){return app.song_files;}
void player_song_files_toggle(void){app.song_files=!app.song_files;state_dirty=1;}
int player_menu_exchange(unsigned menu,unsigned id_a,unsigned id_b){
 if(!(app.menus.enabled&MENU_ORDER_ON)||menu_order_exchange(&app.menus,menu,id_a,id_b))return -1;
 state_dirty=1;return 0;
}
int player_delete_configuration(void){
 /* Stop both producer and the old queue before touching disk. Even on a
  * partial unlink failure, never auto-save RAM over the user's files. */
 audio_stop();started=ended=0;idle_now.valid=0;have_audio=0;
 transition_done=pending_end=transition_end=0;
 struct QueueSource *q=&tmp_src;memset(q,0,sizeof(*q));queue_set(q,0);queue_overlay_import(0,0);
 state_dirty=0;int e=state_delete_configuration();
 memset(&app,0,sizeof(app));app.stereo=1;app.flags=STATE_HOLD_RESET_DONE;menu_order_reset(&app.menus);   /* Hold START to unlock On */
 queue_set_mode(MODE_NORMAL);boost_gain=256;stats.boost=0;stats.browser_order=MODE_NORMAL;
 stats.sound_mode=stats.cache_sound_mode=3;stats.output_mode=0;
 return e;
}
int player_delete_library(void){
 audio_stop();started=ended=0;idle_now.valid=0;have_audio=0;
 transition_done=pending_end=transition_end=0;
 /* A deleted index is a fresh playback start: no old indexed source, current
  * song or Play Next entry may return after the required power cycle. */
 struct QueueSource *q=&tmp_src;memset(q,0,sizeof(*q));queue_set(q,0);queue_overlay_import(0,0);
 app.has_song=0;app.last_song[0]=0;app.queue=*q;app.queue_index=0;state_dirty=0;
 int e=library_delete_index();stats.library_songs=library_count();
 if(pn_delete())e=-1;
 /* Keep unrelated preferences and menu orders. Replace both alternating
  * STATE slots so a corrupt newest slot cannot resurrect the old song. */
 if(state_save(&app)||state_save(&app))e=-1;
 return e;
}
int player_play_next(const char *path){
 if(!started||ended){struct QueueSource *s=&tmp_src;unsigned k=strlen(path);if(k>=STORAGE_PATH)return -1;memset(s,0,sizeof(*s));s->kind=QUEUE_SINGLE;memcpy(s->text,path,k+1);player_play(s,0);return 0;}
 return queue_play_next(path);
}
unsigned player_output_hz(void){
 if(stats.cache_audio&&stats.cache_rate)return stats.cache_rate==44151?44151:stats.cache_rate;
 return 32768;
}
unsigned player_output_mono(void){
 if(stats.cache_audio&&storage_cache_info()->channels==1)return 1;
 return stats.output_mode;
}
/* 11.0: the path hashes (FNV-1a) of the songs just scanned, sorted, in the
 * library's sort buffer: a playlist entry found there exists (Update
 * Library then searches no folder on the card for it). */
static uint32_t *lib_hash;static unsigned lib_hash_n;static struct LibSong hash_song;
static uint32_t path_fnv(const char *p){uint32_t h=2166136261u;while(*p)h=(h^(uint8_t)*p++)*16777619u;return h;}
static int lib_known(const char *p){
 uint32_t h=path_fnv(p);unsigned a=0,b=lib_hash_n;
 while(a<b){unsigned m=a+(b-a)/2;if(lib_hash[m]<h)a=m+1;else b=m;}
 return a<lib_hash_n&&lib_hash[a]==h;
}
static void lib_hash_build(void){
 unsigned cap;lib_hash=(uint32_t*)(void*)library_scratch(&cap);cap/=4;lib_hash_n=0;
 for(uint32_t i=0;i<library_count()&&lib_hash_n<cap;i++){if(!library_song(i,&hash_song))lib_hash[lib_hash_n++]=path_fnv(hash_song.path);}
 for(unsigned gap=lib_hash_n/2;gap;gap=gap==2?1:gap*5/11)   /* Shell sort */
  for(unsigned i=gap;i<lib_hash_n;i++){uint32_t v=lib_hash[i];unsigned j=i;while(j>=gap&&lib_hash[j-gap]>v){lib_hash[j]=lib_hash[j-gap];j-=gap;}lib_hash[j]=v;}
}
int player_update_library(LibraryProgress cb){
 audio_stop();
 unsigned t=ticks();
 int e=library_update(cb);
 /* v0.9.8 telemetry: update time (covers included), pictures decoded. */
 stats.update_cycles=ticks()-t;stats.cover_decodes=library_cover_decodes;stats.library_songs=library_count();
 /* The cover decoder used the decoder overlay: bring the song's decoder back.
  * MP3 state lives in EWRAM (re-copying the code is enough); GAC state was
  * in the overlay's scratch, so a GAC restarts at its paused frame. */
 if(stats.decoder==DECODER_JPEG&&started&&!ended&&!stats.fatal){
  if(stats.cache_audio){exact_frame=stats.track_samples;memcpy(path_buf,slots[stats.current_track&3u].path,STORAGE_PATH);change_track(path_buf,0);}
  else decoder_use(DECODER_MP3);
 }
 if(queue_source()->kind==QUEUE_LIBRARY||queue_source()->kind==QUEUE_WORDS){
  /* Library ranges refer to the old index: keep the current song only. */
  struct QueueSource *s=&tmp_src;memset(s,0,sizeof(*s));s->kind=QUEUE_SINGLE;
  const struct NowPlaying *n=player_now();memcpy(s->text,n->path,STORAGE_PATH);queue_set(s,0);
  memcpy(&app.queue,s,sizeof(*s));app.queue_index=0;state_dirty=1;
 }
 /* 11.0: after a successful scan, playlist entries whose song file is
  * absent are removed (a read error keeps them, playlist_prune). A queue
  * from a playlist follows (its count and position). */
 if(!e&&playlist_count()){
  folder_sort_busy=1;if(cb)cb(9,0);lib_hash_build();   /* the sort buffer is folder-order scratch too */
  for(unsigned i=0;i<playlist_count();i++){
   uint32_t id;unsigned removed;if(playlist_info(i,&id,0))continue;
   if(cb)cb(9,i);
   playlist_prune(id,lib_known,queue_playlist_removed,&removed);
   if(removed&&queue_source()->kind==QUEUE_PLAYLIST&&queue_source()->playlist==id){memcpy(&app.queue,queue_source(),sizeof(app.queue));app.queue_index=queue_index();state_dirty=1;}
  }
  folder_sort_busy=0;folder_sort_epoch++;
 }
 return e;
}
void player_playlist_removed(uint32_t id,uint32_t i){queue_playlist_removed(id,i);}
void player_playlist_moved(uint32_t id,uint32_t a,uint32_t b){queue_playlist_moved(id,a,b);}
/* Library, folder and playlist entries for the queue. */
static struct LibSong resolve_song;
static struct DirCursor queue_dir;
static struct PlaylistEntry resolve_entry;
static int resolve(const struct QueueSource *src,uint32_t i,char *out){
 if(src->kind==QUEUE_LIBRARY){
  uint32_t id;if(src->generation!=library_generation()||library_perm(src->perm,src->start+i,&id)||library_song(id,&resolve_song))return -1;
  memcpy(out,resolve_song.path,STORAGE_PATH);return 0;
 }
 if(src->kind==QUEUE_WORDS){
  uint32_t id;if(src->generation!=library_generation()||library_word_song(src->start,src->playlist,src->perm,i,&id)||library_song(id,&resolve_song))return -1;
  memcpy(out,resolve_song.path,STORAGE_PATH);return 0;
 }
 if(src->kind==QUEUE_FOLDER){
  if(strcmp(queue_dir.path,src->text)||queue_dir.kind!=FOLDER_SONGS)folder_bind(&queue_dir,src->text,FOLDER_SONGS);
  static char name[STORAGE_PATH];if(folder_name(&queue_dir,i,name,sizeof(name)))return -1;
  return folder_join(out,src->text,name)?0:-1;
 }
 if(src->kind==QUEUE_PLAYLIST){if(playlist_entry(src->playlist,i,&resolve_entry)||!resolve_entry.path[0]||!resolve_entry.valid)return -1;   /* missing file: skipped */memcpy(out,resolve_entry.path,STORAGE_PATH);return 0;}
 return -1;
}
/* Called from long UI/library work: latch keys, decode only when the buffer
 * is getting low (4096 samples: 0.5 s of MP3 output, 93 ms at 44151 Hz). */
static void service(unsigned low){
 input_poll((uint16_t)(~R16(0x4000130)&0x3ff));
 for(unsigned k=0;k<2&&!stats.paused&&!stats.fatal&&!transition_end&&wr-rd<low;k++)pump();
}
void player_service(void){service(stats.cache_audio?FILL:8192u);}   /* MP3: 0.25 s at 32768 Hz (v1.1) */
/* v1.8: only below the list floor (1/8 s): drawing a screen behind the blank
 * of a mode change first, the rest of the buffer refilled afterwards. */
void player_service_floor(void){service(SCROLL_MIN_BUFFER);}
/* Before SD writes (playlists): top the ring up so the write cannot drain it.
 * GAC blocks are cheap; MP3 is bounded to about 12 decode steps. */
void player_fill(void){
 for(unsigned k=stats.cache_audio?32u:12u;k&&!stats.paused&&!stats.fatal&&!transition_end&&wr-rd<FILL;k--)pump();
}
/* An underrun pauses the output instead of inventing audio; refill and carry
 * on from the same sample rather than stopping playback. */
static void underrun_recover(void){
 if(stats.fatal!=2||!started||ended||transition_end)return;
 stats.fatal=0;have_audio=0;
 while(wr-rd<(stats.cache_audio?4096u:8192u)&&!stats.fatal&&!transition_end)pump();
 if(!stats.fatal)audio_start();
}
#include "stack_watch.h"
/* Keep the diagnostic in ROM: no decoder placement/stack-budget eviction. */
static __attribute__((noinline)) void memory_check(void){
 static struct StackWatch main_watch,irq_watch;static unsigned initialized;
 volatile const uint32_t*m=(volatile const uint32_t*)&__iwram_end,*q=(volatile const uint32_t*)0x3007e00;
 if(!initialized){stack_watch_init(&main_watch,m,(0x3007d00-(unsigned)m)/4,0xa55aa55a);stack_watch_init(&irq_watch,q,0x1a0/4,0x5aa55aa5);initialized=1;}
 else{stack_watch_update(&main_watch,m,0xa55aa55a);stack_watch_update(&irq_watch,q,0x5aa55aa5);}
 stats.main_margin=main_watch.low*4;stats.irq_margin=irq_watch.low*4;
}
static FILINFO stat_info;
static void restore_state(void){
 state_load(&app);
 folder_song_mask=app.song_types==1?1u:app.song_types==2?2u:3u;   /* v1.8 Show Songs */
 stats.sound_mode=app.stereo?3u:1u;stats.cache_sound_mode=stats.sound_mode;stats.output_mode=player_sound_mono(stats.sound_mode);
 queue_set_mode(app.mode);stats.browser_order=queue_mode;
 boost_apply_level();   /* nothing plays yet: only sets the gain */
 if(!app.has_song)return;
 if(f_stat(app.last_song,&stat_info)!=FR_OK){app.has_song=0;return;}
 static struct QueueSource q;q=app.queue;
 if((q.kind==QUEUE_LIBRARY||q.kind==QUEUE_WORDS)&&q.generation!=library_generation())q.kind=QUEUE_NONE;
 if(q.kind==QUEUE_FOLDER){if(f_stat(q.text,&stat_info)!=FR_OK)q.kind=QUEUE_NONE;else{folder_bind(&queue_dir,q.text,FOLDER_SONGS);int n=folder_count(&queue_dir);q.count=n>0?(uint32_t)n:0;}}
 if(q.kind==QUEUE_PLAYLIST)q.count=playlist_length(q.playlist);
 /* v0.9.5: the Play Next list (QUEUE0/1.DAT) goes straight into the queue's
  * arrays; queue_set() below only resets their count. A last song that came
  * from Play Next keeps its list at the saved index. */
 unsigned on=0,pn=pn_load(&on);
 on=on&&!strcmp(queue_overlay_current(),app.last_song);
 if(q.kind==QUEUE_NONE||app.queue_index>=q.count||(!on&&(queue_resolve(&q,app.queue_index,path_buf)||strcmp(path_buf,app.last_song)))){
  memset(&q,0,sizeof(q));q.kind=QUEUE_SINGLE;memcpy(q.text,app.last_song,STORAGE_PATH);app.queue_index=0;on=0;
 }
 queue_set(&q,app.queue_index);
 /* Songs that no longer exist are dropped, keeping the order. */
 unsigned k=0;
 for(unsigned i=0;i<pn;i++){char *e=queue_overlay_entry(i);if(f_stat(e,&stat_info)==FR_OK){if(k!=i)memcpy(queue_overlay_entry(k),e,STORAGE_PATH);k++;}}
 queue_overlay_import(k,on);
 idle_from(app.last_song);
}
/* Video frames elapsed since the last UI frame, from the 32-bit cycle timer
 * (280896 cycles per frame). Sampling VCOUNT once per loop aliased with
 * ~1-frame MP3 decode steps. Unsigned deltas stay correct across the timer's
 * 256 s wrap, so even a long library update never stalls the UI. */
static unsigned last_frame_tick,frame_acc;
static unsigned new_frame(void){
 unsigned now=ticks(),delta=now-last_frame_tick;last_frame_tick=now;
 if(delta>8u*280896u)delta=8u*280896u;
 frame_acc+=delta;unsigned n=frame_acc/280896u;frame_acc-=n*280896u;
 return n;
}
int main(void){R16(0x4000208)=0;R16(0x4000204)=0x40c0;R16(0x400010a)=0;R16(0x400010e)=0;R16(0x4000108)=0;R16(0x400010c)=0;R16(0x400010e)=0x84;R16(0x400010a)=0x80;
 unsigned sp;__asm__ volatile("mov %0,sp":"=r"(sp));for(unsigned p=(unsigned)&__iwram_end;p<((sp-128)&~3u);p+=4)R32(p)=0xa55aa55a;for(unsigned p=0x3007e00;p<0x3007fa0;p+=4)R32(p)=0x5aa55aa5;
 stats.magic=0x5254504c;stats.min_queue=0xffffffffu;stats.clock_base=1536;stats.clock_rem=0;stats.paused=1;stats.sound_mode=3;stats.cache_sound_mode=3;stats.output_mode=0;
 queue_resolve=resolve;folder_yield=player_service;
 app.stereo=1;menu_order_reset(&app.menus);
 {unsigned n;uint8_t *z=library_scratch(&n);folder_sort_area(&queue_dir,z+n/2+n/8,n/2-n/8);}   /* 24 KB */
 ui_init();
 if(storage_init())stats.fatal=200;
 else{
  /* v2.2 exFAT: entry sets in /, /gbamp3 and GBAMP3.SYS torn by a power cut
   * are repaired before anything reads or makes them (nothing on FAT). */
  /* 11.0: exFAT stores no free-space hint, so the first new cluster after
   * a mount was looked for from the start of the card (a full 2 TiB card:
   * the whole allocation bitmap). gbaSD's alloc hint starts the search
   * after the files in GBAMP3.SYS (one folder read; nothing on FAT32 with
   * FSInfo, or when the folder does not exist yet). */
  {extern void disk_index_writes(unsigned);disk_index_writes(1);(void)library_repair_dir(LIBRARY_DIR);(void)gbasd_alloc_hint(storage_fs(),LIBRARY_DIR);disk_index_writes(0);}
  library_init();stats.library_songs=library_count();playlist_init();
  /* v1.1: create the system folder now (v2.2: or clear its old hidden +
   * system attributes), while no audio plays: making
   * it during playback (first state save on a fresh card) took ~0.6 s of
   * FAT work, longer than the 0.5 s audio buffer at 32768 Hz. A
   * write-protected card just fails here, as the saves do. */
  {extern void disk_index_writes(unsigned);disk_index_writes(1);(void)library_system_dir();disk_index_writes(0);}
  restore_state();}
 #ifdef CONVERT_BENCH   /* test builds only (build.py CONVERT_BENCH=1): time one conversion */
 if(!stats.fatal){stats.conv_result=1000;
  #if CONVERT_BENCH==2
  player_convert("0:/gbamp3/bench.gac","0:/gbamp3/bench_out.mp3",0,0);
  #else
  player_convert("0:/gbamp3/bench.mp3","0:/gbamp3/bench.gac",0,0);
  #endif
 }
 #endif
 stats.ready=1;ui_boot();
 input_reset((~R16(0x4000130))&1023);
 for(;;){stats.loops++;
  if(transition_done&&!stats.fatal){
   transition_done=0;
   if(pending_end){pending_end=0;transition_end=0;have_audio=0;wr=rd=play=0;
    /* Natural end: last song shown paused at 0:00; A restarts the queue. */
    idle_now=slots[stats.current_track&3u];ended=1;stats.paused=1;}
   else{memcpy(path_buf,pending_path,STORAGE_PATH);change_track(path_buf,0);stats.transitions++;audio_start();}
  }
  underrun_recover();
  if(!stats.paused&&wr-rd<FILL&&!stats.fatal)pump();
  input_poll((uint16_t)(~R16(0x4000130)&0x3ff));
  unsigned elapsed=new_frame();
  /* Audio first: while output is nearly drained the UI waits (keys stay
   * latched and the frame count keeps accumulating). */
  if(elapsed&&!stats.paused&&!stats.fatal&&wr-rd<(stats.cache_audio?768u:1536u)){   /* MP3: 47 ms at 32768 Hz (v1.1) */frame_acc+=elapsed*280896u;elapsed=0;}
  if(elapsed){
   unsigned ime=R16(0x4000208);R16(0x4000208)=0;
   unsigned keys=poll_keys()|key_latched;key_latched=0;R16(0x4000208)=ime;
   struct InputFrame in;input_frame((uint16_t)keys,elapsed,&in);
   unsigned a=ticks();ui_frame(&in);stats.ui_cycles+=ticks()-a;
   static unsigned frames;if(!(++frames&7))memory_check();
  }
  if((state_dirty||queue_pn_changed())&&!stats.fatal&&(stats.paused||wr-rd>=FILL-1024u)&&ticks()-state_last>2u*16777216u){
   state_last=ticks();
   if(state_dirty){app.mode=(uint8_t)queue_mode;if(!state_save(&app))state_dirty=0;}
   /* The Play Next list goes with the state it belongs to (it changes rarely). */
   if(queue_pn_changed()&&!stats.fatal)pn_save();
  }
  if(stats.fatal)audio_stop();
  /* v1.9: with the buffer full and no frame due, halt until the next
   * interrupt (the sound interrupt, every 16 samples) instead of spinning.
   * v2.0.1: with interrupts on: a request that comes just before Halt is
   * served first and Halt ends on the next one. With them masked, mGBA
   * (0.10) slept through a request made in the last few cycles until the
   * following one, so a 16-sample refill was skipped (an empty FIFO: a
   * screech). Not while a track's end drains (the sound interrupt may stop
   * itself there, and nothing would end Halt). */
  if(!elapsed&&!stats.paused&&!stats.fatal&&wr-rd>=FILL&&!transition_end&&R16(0x4000200)&16){
   unsigned h=ticks();__asm__ volatile("swi 0x020000":::"r0","r1","r2","r3","memory");stats.halt_cycles+=ticks()-h;
  }
 }
}
