/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4: per-block adaptive LPC (order <= 8),
 * Huffman-coded residuals, a variable number of frames per 1024-byte block;
 * mid + half-rate side stereo or mid-only mono. See GAC_FORMAT.md;
 * tools/gac4.py holds the encoder and the reference decoder, and
 * tests/test_gac4.py requires them to agree bit for bit. */
#include "gac4.h"
#ifdef __arm__
/* Hot code and its tables live in the GAC decoder overlay (build.py renames
 * .iwram* of this object to .ov_gac*); the state in its NOLOAD scratch. */
#define GAC_FAST __attribute__((section(".iwram"),target("arm"),noinline))
#define GAC_TABLE __attribute__((section(".iwram.gac_tables"),aligned(4)))
#define GAC_SCRATCH __attribute__((section(".gac_scratch"),aligned(4)))
#else
#define GAC_FAST
#define GAC_TABLE
#define GAC_SCRATCH
#endif
#include "gac4_tables.h"
#include "boost.h"
static const uint16_t lut[4][1024] GAC_TABLE={GAC4_LUT0,GAC4_LUT1,GAC4_LUT2,GAC4_LUT3};
static const uint16_t header_lut[1024] GAC_TABLE=GAC4_HEADER_LUT;
static const int32_t steps[64] GAC_TABLE=GAC4_STEPS;
static const int16_t reflect[256]=GAC4_REFLECT;
struct Gac4 gac4 GAC_SCRATCH;
#define DELTA 6
#define ZERO_SYMBOL 52
#define ESC_SYMBOL 53
#define PAIR_TABLES 2

static unsigned le32(const unsigned char*p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static unsigned fnv(unsigned hash,const unsigned char*p,unsigned n){for(unsigned j=0;j<n;j++)hash=(hash^p[j])*16777619u;return hash;}
unsigned gac_fnv(unsigned hash,const unsigned char*p,unsigned n){return fnv(hash,p,n);}
int gac_header(const unsigned char*p,unsigned bytes,unsigned file_size,struct GacInfo*out){
 static const unsigned char magic[8]={'G','B','C','A','C','H','E',0};
 if(bytes<16)return 0;
 for(unsigned j=0;j<8;j++)if(p[j]!=magic[j])return 0;
 if(!p[9]&&p[8]>=1&&p[8]<=3)return -1;                /* GAC v1..v3: reconvert */
 if(bytes!=GAC_HEADER||p[8]!=4||p[9]||p[10]!=64||p[11]||le32(p+12)!=3||le32(p+24)!=GAC_BLOCK||le32(p+44)!=512)return 0;
 unsigned rate=le32(p+16),channels=le32(p+20),blocks=le32(p+28),total=le32(p+32),seek=le32(p+36),audio=le32(p+40),art=le32(p+48);
 if(rate!=32768&&rate!=44151)return 0;
 if(channels!=1&&channels!=2)return 0;
 if(art==0){if(seek!=576||le32(p+56))return 0;}else if((art!=2048&&art!=8192)||seek!=576+art)return 0;   /* v1.8: 64x64 covers too */
 if(!total||total>0x7fffffffu||!blocks||blocks>total||blocks>(0x7fffffffu-seek)/GAC_BLOCK/2)return 0;
 if((total-1)/blocks>=GAC_MAX_FRAMES)return 0;       /* cannot hold the frames */
 if(audio!=((seek+4*blocks+511)&~511u)||file_size!=audio+blocks*GAC_BLOCK)return 0;
 out->rate=rate;out->total=total;out->blocks=blocks;out->channels=channels;out->seek=seek;out->audio=audio;out->meta=64;out->art=art?576:0;out->art_bytes=art;
 return 1;
}
/* One NUL-terminated field: valid UTF-8 without controls, zero after the NUL. */
static int utf8_field(const unsigned char*p,unsigned n){
 unsigned j=0;
 while(j<n&&p[j]){
  unsigned a=p[j],k=a<0x80?0:a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:4;
  if(k==4||(a<0x20)||a==0x7f||j+k>=n)return 0;
  unsigned c=a&(k?(0x3fu>>k):0x7fu);
  for(unsigned i=1;i<=k;i++){unsigned b=p[j+i];if(b<0x80||b>0xbf)return 0;c=c<<6|(b&63);}
  if((k==2&&c<0x800)||(k==3&&(c<0x10000||c>0x10ffff))||(c>=0xd800&&c<=0xdfff)||(c>=0x80&&c<0xa0))return 0;
  j+=k+1;
 }
 if(j>=n)return 0;
 for(;j<n;j++)if(p[j])return 0;
 return 1;
}
int gac_metadata_valid(const unsigned char*h,const unsigned char*p){
 static const unsigned char magic[8]={'G','A','C','M','E','T','A','3'};
 if(h[8]!=4||fnv(2166136261u,p,512)!=le32(h+52))return 0;
 for(unsigned j=0;j<8;j++)if(p[j]!=magic[j])return 0;
 /* GACMETA3: u16 track, track total, disc, disc total; UTF-8 title 128,
  * artist 96, album 96, album artist 96, genre 64; bytes 496..511 zero. */
 static const unsigned short at[5]={16,144,240,336,432},len[5]={128,96,96,96,64};
 for(unsigned f=0;f<5;f++)if(!utf8_field(p+at[f],len[f]))return 0;
 for(unsigned j=496;j<512;j++)if(p[j])return 0;
 return 1;
}
int gac_cover_valid(const unsigned char*h,const unsigned char*p){
 unsigned n=le32(h+48);return h[8]==4&&(n==2048||n==8192)&&fnv(2166136261u,p,n)==le32(h+56);
}
int gac_seek_check(struct GacSeekCheck*c,const unsigned char*e,unsigned n,const struct GacInfo*info){
 if(!c->count&&!c->hash){c->hash=2166136261u;c->prev=0;}
 for(unsigned j=0;j<n;j++){
  unsigned v=le32(e+4*j);
  if(c->count==0?v!=0:(v<=c->prev||v-c->prev>GAC_MAX_FRAMES))return 0;
  if(v>=info->total||c->count>=info->blocks)return 0;
  c->prev=v;c->count++;
 }
 c->hash=fnv(c->hash,e,4*n);
 return 1;
}
int gac_seek_done(const struct GacSeekCheck*c,const unsigned char*h,const struct GacInfo*info){
 return c->count==info->blocks&&info->total-c->prev<=GAC_MAX_FRAMES&&c->hash==le32(h+60);
}
unsigned gac_block_frames(const unsigned char*b){
 unsigned n=(unsigned)b[0]|((unsigned)b[1]<<8);
 return n<1?1:n>GAC_MAX_FRAMES?GAC_MAX_FRAMES:n;
}
static int s16(const unsigned char*p){return (int16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static int clamp16(int v){return v>32767?32767:v<-32768?-32768:v;}
/* Reflection coefficient codes -> Q12 direct form, as gac4.step_up. */
static void step_up(const unsigned char*codes,unsigned n,int32_t*c){
 int32_t a[8];
 for(unsigned m=0;m<n;m++){
  int k=reflect[codes[m]];
  for(unsigned i=0;i<m;i++)c[i]=clamp16(a[i]-((k*a[m-1-i]+16384)>>15));
  c[m]=(k+4)>>3;
  for(unsigned i=0;i<=m;i++)a[i]=c[i];
 }
 for(unsigned i=n;i<8;i++)c[i]=0;
}
static void stream_begin(struct Gac4Stream*s,int32_t*x,unsigned order,const unsigned char*refl,const unsigned char*hist,unsigned idx){
 s->order=order;step_up(refl,order,s->c);
 /* x[7] is the newest sample; the block stores newest first. */
 for(unsigned k=0;k<8;k++)x[7-k]=k<order?s16(hist+2*k):0;
 s->idx=idx<1?1:idx>63?63:idx;
}
void gac4_begin(unsigned channels,unsigned skip){
 struct Gac4*g=&gac4;const unsigned char*b=(const unsigned char*)g->block;
 unsigned om=b[2]&15,so=b[2]>>4;
 if(om>8)om=8;
 if(so>8)so=8;
 g->channels=channels;g->side=channels==2&&!(b[3]&1);
 if(!g->side)so=0;
 unsigned pos=6,refl=pos;pos+=(om+so+1)&~1u;
 stream_begin(&g->m,g->mx,om,b+refl,b+pos,b[4]);
 stream_begin(&g->s,g->sx,so,b+refl+om,b+pos+2*om,b[5]);
 pos+=2*(om+so);
 g->prev=so?g->sx[7]:0;
 g->p=(const uint16_t*)(b+pos);g->end=(const uint16_t*)(b+GAC_BLOCK);g->acc=0;g->cnt=0;
 g->skip=skip;
}
/* ---- hot path (GAC overlay) ----
 * The bit reader lives in locals (acc: next bits LSB first, cnt of them);
 * halfwords past the block end read as zero. */
#define REFILL if(cnt<16){acc|=(uint32_t)(p<end?*p++:0)<<cnt;cnt+=16;}
#define CODE(t,out) {REFILL unsigned e_=(t)[acc&1023],n_=e_>>8;acc>>=n_;cnt-=n_;out=e_&255;}
#define RAW(n,out) {REFILL out=acc&((1u<<(n))-1);acc>>=(n);cnt-=(n);}
#define RAW12(out) {unsigned r_;RAW(12,r_);out=(int)r_-(int)((r_&2048)<<1);}
/* Residuals of one subblock times its step into q[32]; returns 0 for a zero
 * subblock (q is then left untouched). */
static int GAC_FAST residuals(struct Gac4*g,struct Gac4Stream*s,int*q){
 uint32_t acc=g->acc;unsigned cnt=g->cnt;const uint16_t*p=g->p,*end=g->end;
 unsigned sym,tab;int step=0;
 CODE(header_lut,sym);
 if(sym!=ZERO_SYMBOL){
  if(sym>=ESC_SYMBOL){unsigned v;tab=sym-ESC_SYMBOL;RAW(6,v);s->idx=v<1?1:v;}
  else{tab=sym&3;int v=(int)s->idx+(int)(sym>>2)-DELTA;s->idx=v<1?1:v>63?63:(unsigned)v;}
  const uint16_t*t=lut[tab];
  step=steps[s->idx];
  if(tab<PAIR_TABLES){
   for(int*e=q+32;q<e;q+=2){
    unsigned v;CODE(t,v);
    if(v==0x88){int a,b;RAW12(a);RAW12(b);q[0]=a*step;q[1]=b*step;}
    else{q[0]=((int)(v<<24)>>28)*step;q[1]=((int)(v<<28)>>28)*step;}
   }
  }else for(int*e=q+32;q<e;q++){
   unsigned v;CODE(t,v);
   int a;if(v==0x80)RAW12(a)else a=(int8_t)v;
   *q=a*step;
  }
 }
 g->acc=acc;g->cnt=cnt;g->p=p;
 return step;
}
/* x[i] = clamp16(((sum c[k] x[i-1-k] + 2048) mod 2^32) >> 12 + r[i]),
 * r[i] = q[i]*step; x[-8..-1] is the history. Unrolled per order; order 8
 * (the usual one) is gac4_synth8 in gac4_arm.S on the GBA. */
#define TAP(k) +c[k]*x[i-1-k]
#define SYN(TAPS) for(int i=0;i<32;i++){int a=2048 TAPS;int v=(a>>12)+q[i];x[i]=v>32767?32767:v<-32768?-32768:v;}
#ifdef __arm__
void gac4_synth8(int32_t*x,const int32_t*c,const int*r);
struct Gac4Boost {int el,er,prev,gain,dr;};
void gac4_boost64(const int32_t*m,const int32_t*sd,int8_t*l,struct Gac4Boost*b);
#endif
static void GAC_FAST synth(int32_t*x,const int32_t*c,unsigned order,const int*q){
#ifdef __arm__
 if(order>=8){gac4_synth8(x,c,q);return;}
#endif
 switch(order){
  case 0:SYN();break;
  case 1:SYN(TAP(0));break;
  case 2:SYN(TAP(0)TAP(1));break;
  case 3:SYN(TAP(0)TAP(1)TAP(2));break;
  case 4:SYN(TAP(0)TAP(1)TAP(2)TAP(3));break;
  case 5:SYN(TAP(0)TAP(1)TAP(2)TAP(3)TAP(4));break;
  case 6:SYN(TAP(0)TAP(1)TAP(2)TAP(3)TAP(4)TAP(5));break;
  case 7:SYN(TAP(0)TAP(1)TAP(2)TAP(3)TAP(4)TAP(5)TAP(6));break;
  default:SYN(TAP(0)TAP(1)TAP(2)TAP(3)TAP(4)TAP(5)TAP(6)TAP(7));break;
 }
}
static void GAC_FAST subblock(struct Gac4*g,struct Gac4Stream*s,int32_t*x,int*q){
 if(!residuals(g,s,q))for(unsigned i=0;i<32;i++)q[i]=0;
 synth(x,s->c,s->order,q);
}
/* Parse a side subblock without synthesis (mono output of a stereo file). */
static void GAC_FAST skip_subblock(struct Gac4*g,struct Gac4Stream*s,int*q){residuals(g,s,q);}
/* First-order error feedback to signed 8-bit (L = M + S, R = M - S, or M),
 * as v3: w = clamp(v + e, -32768, 32639), q = (w + 128) >> 8, e = w - 256 q. */
#define SHAPE(v,e,out) {int sv_=(v)+e;if(sv_>32639)sv_=32639;else if(sv_<-32768)sv_=-32768;int sq_=(sv_+128)>>8;e=sv_-(sq_<<8);out=(int8_t)sq_;}
/* One unit of output: frames i..n-1 of the 64 decoded ones, with gain g
 * (G is 256 when the caller wants the unchanged 0 dB loop). */
#define BOOSTED(v) (G==256?(v):boost_apply((v),G))
#define EMIT(vl,vr) {unsigned a_=w&mask;SHAPE(BOOSTED(vl),el,L[a_]);SHAPE(BOOSTED(vr),er,Rr[a_]);tag[a_]=tagv;w++;}
struct EmitState {int8_t*l,*r;uint8_t*tag;unsigned w,mask;int el,er,prev;uint8_t tagv;};
static inline __attribute__((always_inline)) void emit_unit(struct EmitState*e,const int32_t*m,const int32_t*sd,unsigned stereo,unsigned i,unsigned n,const int G){
 int8_t*L=e->l,*Rr=e->r;uint8_t*tag=e->tag;uint8_t tagv=e->tagv;
 unsigned w=e->w,mask=e->mask;int el=e->el,er=e->er,prev=e->prev;
 if(stereo){
  /* Side sample k is frame 2k+1; even frames interpolate causally.
   * Boosted full units take gac4_boost64 on the GBA instead. */
  if(G==256&&i==0&&n==64)for(unsigned k=0;k<32;k++){
   int s1=sd[k],s0=(prev+s1)>>1,m0=m[2*k],m1=m[2*k+1];prev=s1;
   EMIT(m0+s0,m0-s0);EMIT(m1+s1,m1-s1);
  }else for(unsigned f=0;f<64;f+=2){
   int s1=sd[f>>1],s0=(prev+s1)>>1,m0=m[f],m1=m[f+1];prev=s1;
   if(f>=i&&f<n)EMIT(m0+s0,m0-s0);
   if(f+1>=i&&f+1<n)EMIT(m1+s1,m1-s1);
  }
 }else{
  for(unsigned f=i;f<n;f++){unsigned a_=w&mask;int8_t v;SHAPE(BOOSTED(m[f]),el,v);L[a_]=v;Rr[a_]=v;tag[a_]=tagv;w++;}
  er=el;
 }
 e->w=w;e->el=el;e->er=er;e->prev=prev;
}
static void GAC_FAST emit_plain(struct EmitState*e,const int32_t*m,const int32_t*sd,unsigned stereo,unsigned i,unsigned n){emit_unit(e,m,sd,stereo,i,n,256);}
static void GAC_FAST __attribute__((optimize("Os"))) emit_boost(struct EmitState*e,const int32_t*m,const int32_t*sd,unsigned stereo,unsigned i,unsigned n,int g){
#ifdef __arm__
 /* A full stereo unit whose 64 ring slots do not wrap: the ARM loop
  * (gac4_arm.S) keeps everything in registers; the tags are written here. */
 unsigned a=e->w&e->mask;
 if(stereo&&i==0&&n==64&&a+64<=e->mask+1){
  struct Gac4Boost b={e->el,e->er,e->prev,g,(int)(e->r-e->l)};
  gac4_boost64(m,sd,e->l+a,&b);
  uint8_t*t=e->tag+a,*te=t+64;uint32_t tv=e->tagv*0x01010101u;
  while(((uintptr_t)t&3)&&t<te)*t++=e->tagv;
  for(;te-t>=4;t+=4)*(uint32_t*)t=tv;
  while(t<te)*t++=e->tagv;
  e->w+=64;e->el=b.el;e->er=b.er;e->prev=b.prev;
  return;
 }
#endif
 emit_unit(e,m,sd,stereo,i,n,g);
}
unsigned GAC_FAST gac4_decode(unsigned units,unsigned frames_left,struct GacOut*o){
 struct Gac4*g=&gac4;
 int32_t*mx=g->mx,*sx=g->sx;int*q=g->q;
 struct EmitState e={o->l,o->r,o->tag,o->w,o->mask,o->el,o->er,g->prev,(uint8_t)o->tagv};
 unsigned emitted=0,stereo=g->channels==2&&!o->mono;int gain=o->gain;
 for(unsigned u=0;u<units&&frames_left;u++){
  subblock(g,&g->m,mx+8,q);subblock(g,&g->m,mx+40,q);
  if(g->side){if(stereo)subblock(g,&g->s,sx+8,q);else skip_subblock(g,&g->s,q);}
  else for(unsigned k=8;k<40;k++)sx[k]=0;
  unsigned n=frames_left<64?frames_left:64,i=0;
  frames_left-=n;
  if(g->skip){i=g->skip<n?g->skip:n;g->skip-=i;}
  if(gain==256)emit_plain(&e,mx+8,sx+8,stereo,i,n);else emit_boost(&e,mx+8,sx+8,stereo,i,n,gain);
  emitted+=n-i;
  for(unsigned k=0;k<8;k++){mx[k]=mx[64+k];sx[k]=sx[32+k];}
 }
 o->w=e.w;o->el=e.el;o->er=e.er;g->prev=e.prev;
 return emitted;
}
/* v2.0 conversion (GAC -> MP3): the decoded samples at full precision
 * (L = M + S, R = M - S clamped to 16 bits; side interpolated as the
 * player's output) instead of the 8-bit ring. Runs from ROM; the hot
 * parts are the overlay's. */
unsigned gac4_decode16(unsigned units,unsigned frames_left,int16_t *L,int16_t *R){
 struct Gac4*g=&gac4;
 int32_t*mx=g->mx,*sx=g->sx;int*q=g->q;
 unsigned emitted=0,stereo=g->channels==2;int prev=g->prev;
 for(unsigned u=0;u<units&&frames_left;u++){
  subblock(g,&g->m,mx+8,q);subblock(g,&g->m,mx+40,q);
  if(g->side)subblock(g,&g->s,sx+8,q);
  else for(unsigned k=8;k<40;k++)sx[k]=0;
  unsigned n=frames_left<64?frames_left:64,i=0;
  frames_left-=n;
  if(g->skip){i=g->skip<n?g->skip:n;g->skip-=i;}
  const int32_t*m=mx+8,*sd=sx+8;
  for(unsigned f=0;f<64;f+=2){
   int s1=stereo?sd[f>>1]:0,s0=(prev+s1)>>1;prev=s1;
   for(unsigned h=0;h<2;h++){unsigned ff=f+h;int sv=h?s1:s0;
    if(ff>=i&&ff<n){L[emitted]=(int16_t)clamp16(m[ff]+sv);R[emitted]=(int16_t)clamp16(m[ff]-sv);emitted++;}}
  }
  for(unsigned k=0;k<8;k++){mx[k]=mx[64+k];sx[k]=sx[32+k];}
 }
 g->prev=prev;
 return emitted;
}
