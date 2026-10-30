/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Baseline JPEG -> 64x64 RGB555 cover (v1.8; 32x32 from
 * the DC coefficients only before).
 * See jpeg_dc.h. Supported: SOF0/SOF1 8-bit, 1 (grey) or 3 components
 * (YCbCr, or RGB with an Adobe APP14 transform 0), sampling factors 1-2,
 * one interleaved scan, restart intervals. Everything else is rejected.
 * The arithmetic below is mirrored exactly by tools/jpeg_dc.py. */
#include "jpeg_dc.h"
#include "cover.h"
#include <string.h>
#ifdef __arm__
/* v0.9.8: the entropy decoder runs as ARM from IWRAM in a third decoder
 * overlay (build.py renames this object's .iwram* to .ov_jpeg*), loaded
 * while Update Library runs (audio stopped); its Huffman tables sit in the
 * overlay's NOLOAD scratch. Parsing stays in ROM (Thumb). */
#include "overlay.h"
#define JPEG_FAST __attribute__((section(".iwram"),target("arm"),noinline))
#define JPEG_SCRATCH __attribute__((section(".jpeg_scratch"),aligned(4)))
#else
#define JPEG_FAST
#define JPEG_SCRATCH
#endif

struct Huff {uint16_t fast[512];int32_t maxcode[18],mincode[17];uint16_t valptr[17];uint8_t vals[256],ok;};
struct Comp {uint8_t id,h,v,tq,td,ta;int pred;};
struct State {
 JpegRead read;JpegStop stop;void *ctx;
 uint32_t size,pos;unsigned bn,bi;int err;
 uint32_t bits;int nbits,marker,eof,fake;   /* fake: zero bytes fed past the end */
 uint16_t q[4][64];unsigned qset;       /* v1.8: whole tables, zigzag order */
 uint8_t zslot[64];                    /* zigzag index -> coefficient slot (u, v in 0,1,3,5,7), 255: not used */
 struct Huff *h;                       /* jpeg_huff: 0-3 DC, 4-7 AC */
 struct Comp c[3];unsigned nc,w,hgt,hmax,vmax,restart,frame,rgb;
 unsigned gw,gh,ox,oy;
 uint32_t (*acc)[3];                   /* caller's work area (EWRAM) */
 uint8_t cx[JPEG_MAX_SIDE/4],cy[JPEG_MAX_SIDE/4];   /* first grid cell of each 4-pixel unit column/row */
 uint16_t wx[JPEG_MAX_SIDE/4],wy[JPEG_MAX_SIDE/4];  /* overlap with that cell (scaled units); the rest is in the next */
 unsigned big;                         /* cells at least 4 pixels: a unit touches at most 2x2 cells */
 int co[25];                           /* the block's used coefficients (dequantized), by slot */
 uint8_t sub[3][16];                   /* per component, per unit of the MCU: block*4 + quarter (no divisions per unit) */
 unsigned us;                          /* unit: 1 << us pixels square; 2: block quarters, 3: whole blocks (cells of 16+ pixels) */
 uint8_t buf[2048];
};
_Static_assert(JPEG_SIDE*JPEG_SIDE*3*4<=JPEG_WORK_BYTES,"JPEG work area");
#define G JPEG_SIDE
/* Decoder state and tables: IWRAM scratch of the cover overlay on the GBA. */
/* v2.1: after a decode the same IWRAM holds the cover reduction's pixels. */
static union {struct {struct State st;struct Huff huff[8];} d;uint32_t px[COVER_PIXELS];} jpeg_mem JPEG_SCRATCH;
#define jpeg_state jpeg_mem.d.st
#define jpeg_huff jpeg_mem.d.huff

static JPEG_FAST int byte(struct State *s){
 if(s->bi==s->bn){
  if(s->pos>=s->size){s->eof=1;return -1;}
  unsigned k=s->size-s->pos<sizeof(s->buf)?s->size-s->pos:sizeof(s->buf);
  int n=s->read(s->ctx,s->pos,s->buf,k);
  if(n<=0||(unsigned)n>k){s->err=JPEG_ERR_IO;s->eof=1;return -1;}
  s->bn=(unsigned)n;s->bi=0;s->pos+=(unsigned)n;
 }
 return s->buf[s->bi++];
}
static int u16(struct State *s){int a=byte(s),b=byte(s);return a<0||b<0?-1:a<<8|b;}
static int skip(struct State *s,unsigned n){
 unsigned in=s->bn-s->bi;
 if(n<=in){s->bi+=n;return 0;}
 n-=in;s->bi=s->bn=0;
 if(n>s->size-s->pos){s->eof=1;return -1;}
 s->pos+=n;return 0;
}
static int build(struct Huff *t,const uint8_t counts[16],unsigned total){
 memset(t->fast,0,sizeof(t->fast));
 uint32_t code=0;unsigned k=0;
 for(unsigned len=1;len<=16;len++){
  t->valptr[len]=(uint16_t)k;t->mincode[len]=(int32_t)code;
  for(unsigned i=0;i<counts[len-1];i++){
   if(code>=(1u<<len))return -1;       /* over-subscribed table */
   if(len<=9){unsigned base=code<<(9-len),n=1u<<(9-len);for(unsigned j=0;j<n;j++)t->fast[base+j]=(uint16_t)(len<<8|t->vals[k]);}
   code++;k++;
  }
  t->maxcode[len]=counts[len-1]?(int32_t)code-1:-1;
  code<<=1;
 }
 t->maxcode[17]=0x7fffffff;
 t->ok=k==total;return t->ok?0:-1;
}
/* ---- entropy-coded data ---- */
/* Entropy-coded bytes: 0xFF 0x00 is a data 0xFF, 0xFF + other is a marker
 * (then zeros are fed); past the end zeros are fed and counted (`fake`). */
static JPEG_FAST int slow_byte(struct State *s){
 if(s->marker)return 0;
 int b=byte(s);
 if(b<0){s->fake++;return 0;}              /* past the end: truncated if consumed */
 if(b==0xff){
  int c=byte(s);while(c==0xff)c=byte(s);
  if(c==0)return 0xff;
  if(c<0){s->fake++;return 0;}
  s->marker=c;return 0;
 }
 return b;
}
/* Codes longer than the 9-bit table: (length << 8 | symbol), or -1. */
static JPEG_FAST int slow_code(const struct Huff *t,uint32_t bits){
 for(unsigned len=10;len<=16;len++){
  int32_t code=(int32_t)(bits>>(32-len));
  if(t->maxcode[len]>=0&&code<=t->maxcode[len]&&code>=t->mincode[len])return (int)(len<<8|t->vals[t->valptr[len]+code-t->mincode[len]]);
 }
 return -1;
}
#define NEXT_BYTE() (!s->marker&&s->bi<s->bn&&s->buf[s->bi]!=0xff?s->buf[s->bi++]:slow_byte(s))
#define REFILL() while(n<=24){bits|=(uint32_t)NEXT_BYTE()<<(24-n);n+=8;}
/* One block: the DC coefficient (quantized, predicted) to *dc; of the AC
 * coefficients those that shape the four 4x4 means (horizontal and vertical
 * frequencies 0, 1, 3, 5, 7) are dequantized into s->co (their slots set in
 * *used), the rest decoded and skipped. The bit buffer stays in registers. */
static JPEG_FAST int block(struct State *s,struct Comp *c,int *dc,unsigned *used){
 uint32_t bits=s->bits;int n=s->nbits,ret=-1;
 const struct Huff *t=&s->h[c->td];
 REFILL();
 unsigned e=t->fast[bits>>23];
 if(!e){int x=slow_code(t,bits);if(x<0)goto out;e=(unsigned)x;}
 unsigned len=e>>8,cat=e&255;bits<<=len;n-=(int)len;
 if(cat>11)goto out;
 int diff=0;
 if(cat){REFILL();diff=(int)(bits>>(32-cat));bits<<=cat;n-=(int)cat;if(diff<(1<<(cat-1)))diff+=1-(1<<cat);}
 int p=c->pred+diff;
 if(p>2048)p=2048;else if(p<-2048)p=-2048;
 c->pred=p;*dc=p;
 t=&s->h[4+c->ta];
 for(unsigned k=1;k<64;){
  REFILL();
  e=t->fast[bits>>23];
  if(!e){int x=slow_code(t,bits);if(x<0)goto out;e=(unsigned)x;}
  len=e>>8;bits<<=len;n-=(int)len;
  unsigned r=(e>>4)&15,sz=e&15;
  if(!sz){if(r!=15)break;k+=16;continue;}
  if(sz>10)goto out;
  if(n<(int)sz)REFILL();
  k+=r;
  if(k<64&&s->us==2&&s->zslot[k]!=255){
   int v=(int)(bits>>(32-sz));if(v<(1<<(sz-1)))v+=1-(1<<sz);
   v*=(int)s->q[c->tq][k];if(v>4095)v=4095;else if(v<-4095)v=-4095;
   unsigned slot=s->zslot[k];s->co[slot]=v;*used|=1u<<slot;
  }
  bits<<=sz;n-=(int)sz;
  k++;
 }
 ret=0;
out:
 s->bits=bits;s->nbits=n;return ret;
}
static inline int clamp8(int v){return v<0?0:v>2040?2040:v;}
static JPEG_FAST void put(struct State *s,unsigned px,unsigned py,const int v8[3]){
 int r,g,b;
 if(s->nc==1)r=g=b=clamp8(v8[0]);
 else if(s->rgb){r=clamp8(v8[0]);g=clamp8(v8[1]);b=clamp8(v8[2]);}
 else{
  int y=clamp8(v8[0]),cb=clamp8(v8[1])-1024,cr=clamp8(v8[2])-1024;
  r=clamp8(y+((91881*cr+32768)>>16));
  g=clamp8(y+((-22554*cb-46802*cr+32768)>>16));
  b=clamp8(y+((116130*cb+32768)>>16));
 }
 uint32_t rgb[3]={(uint32_t)(r+4)>>3,(uint32_t)(g+4)>>3,(uint32_t)(b+4)>>3};
 if(s->big){
  /* Same weights as the general loop below, without its multiplications. */
  unsigned u=1u<<s->us,bx=px>>s->us,by=py>>s->us,ix=s->cx[bx],iy=s->cy[by];
  uint32_t ax=s->wx[bx],ay=s->wy[by];
  uint32_t fx=((px+u<s->w?px+u:s->w)-px)*s->gw,fy=((py+u<s->hgt?py+u:s->hgt)-py)*s->gh;   /* full unit extent */
  uint32_t (*acc)[3]=s->acc+(s->oy+iy)*G+s->ox+ix;
  uint32_t w00=ax*ay;acc[0][0]+=rgb[0]*w00;acc[0][1]+=rgb[1]*w00;acc[0][2]+=rgb[2]*w00;
  if(fx>ax){uint32_t w=(fx-ax)*ay;acc[1][0]+=rgb[0]*w;acc[1][1]+=rgb[1]*w;acc[1][2]+=rgb[2]*w;}
  if(fy>ay){uint32_t w=ax*(fy-ay);acc[G][0]+=rgb[0]*w;acc[G][1]+=rgb[1]*w;acc[G][2]+=rgb[2]*w;
   if(fx>ax){w=(fx-ax)*(fy-ay);acc[G+1][0]+=rgb[0]*w;acc[G+1][1]+=rgb[1]*w;acc[G+1][2]+=rgb[2]*w;}}
  return;
 }
 /* Area overlap of the unit's pixels with each grid cell, in units scaled
  * by the grid size (cell i spans [i*w, (i+1)*w) on the x axis). */
 unsigned u=1u<<s->us,x1=px+u<s->w?px+u:s->w,y1=py+u<s->hgt?py+u:s->hgt;
 uint32_t X0=px*s->gw,X1=x1*s->gw,Y0=py*s->gh,Y1=y1*s->gh;
 for(unsigned iy=s->cy[py>>s->us];iy<s->gh&&iy*s->hgt<Y1;iy++){
  uint32_t a=iy*s->hgt>Y0?iy*s->hgt:Y0,bnd=(iy+1)*s->hgt<Y1?(iy+1)*s->hgt:Y1,oy=bnd-a;
  for(unsigned ix=s->cx[px>>s->us];ix<s->gw&&ix*s->w<X1;ix++){
   uint32_t c0=ix*s->w>X0?ix*s->w:X0,c1=(ix+1)*s->w<X1?(ix+1)*s->w:X1,wt=(c1-c0)*oy;
   uint32_t *acc=s->acc[(s->oy+iy)*G+s->ox+ix];
   acc[0]+=rgb[0]*wt;acc[1]+=rgb[1]*wt;acc[2]+=rgb[2]*wt;
  }
 }
}
static JPEG_FAST int restart_marker(struct State *s){
 s->bits=0;s->nbits=0;
 if(!s->marker){
  /* Remaining padding bytes, then the RSTn marker. */
  for(unsigned n=0;n<16&&!s->marker;n++){
   int b=byte(s);if(b<0)return -1;
   if(b==0xff){int c=byte(s);while(c==0xff)c=byte(s);if(c<0)return -1;if(c)s->marker=c;}
  }
 }
 if(s->marker<0xd0||s->marker>0xd7)return -1;
 s->marker=0;
 for(unsigned i=0;i<s->nc;i++)s->c[i].pred=0;
 return 0;
}
/* The four 4x4 means of a block (v8 scale: 8 x the sample value) from its DC
 * value and used coefficients: F(u,v) weighted by A(u)A(v), A the mean of
 * the DCT basis over a half block (x 1024: 1024, 928, -326, 218, -185 for
 * frequency 0, 1, 3, 5, 7), negated on the right / lower half for odd
 * frequencies; rows first, each stage rounded. */
#define ODD(a,b,c,d) ((a)*928-(b)*326+(c)*218-(d)*185)
static JPEG_FAST void quads(struct State *s,int dc,unsigned used,int out[4]){
 if(!used){out[0]=out[1]=out[2]=out[3]=dc+1024;return;}
 int *co=s->co;if(dc>4095)dc=4095;else if(dc<-4095)dc=-4095;
 co[0]=dc;
 int L[5],R[5];
 for(unsigned v=0;v<5;v++){
  int e=co[v]*1024,o=ODD(co[5+v],co[10+v],co[15+v],co[20+v]);
  L[v]=(e+o+512)>>10;R[v]=(e-o+512)>>10;
 }
 int e=L[0]*1024,o=ODD(L[1],L[2],L[3],L[4]);out[0]=((e+o+512)>>10)+1024;out[2]=((e-o+512)>>10)+1024;
 e=R[0]*1024;o=ODD(R[1],R[2],R[3],R[4]);out[1]=((e+o+512)>>10)+1024;out[3]=((e-o+512)>>10)+1024;
 co[0]=0;for(unsigned k=1;k<25;k++)if(used>>k&1)co[k]=0;
}
static JPEG_FAST int scan(struct State *s){
 unsigned mw=8*s->hmax,mh=8*s->vmax,mx=(s->w+mw-1)/mw,my=(s->hgt+mh-1)/mh,count=0;
 for(unsigned y=0;y<my;y++){
  if(s->stop&&s->stop(s->ctx))return JPEG_ERR_TIME;
  for(unsigned x=0;x<mx;x++){
   if(s->restart&&count&&count%s->restart==0&&restart_marker(s))return JPEG_ERR_DATA;
   count++;
   int q4[3][4][4];
   for(unsigned i=0;i<s->nc;i++){struct Comp *c=&s->c[i];
    for(unsigned v=0;v<c->v;v++)for(unsigned h=0;h<c->h;h++){
     int d;unsigned used=0;if(block(s,c,&d,&used))return JPEG_ERR_DATA;quads(s,d*(int)s->q[c->tq][0],used,q4[i][v*2+h]);}}
   if(s->err)return s->err;
   if(s->fake&&s->nbits<8*s->fake)return JPEG_ERR_DATA;   /* used bits past the end */
   unsigned nx=mw>>s->us,ny=mh>>s->us;
   for(unsigned uy=0;uy<ny;uy++)for(unsigned ux=0;ux<nx;ux++){
    unsigned px=x*mw+(ux<<s->us),py=y*mh+(uy<<s->us);
    if(px>=s->w||py>=s->hgt)continue;
    int v8[3];
    for(unsigned i=0;i<s->nc;i++){unsigned k=s->sub[i][uy*4+ux];v8[i]=q4[i][k>>2][k&3];}
    put(s,px,py,v8);
   }
  }
 }
 return JPEG_OK;
}
/* Cell sums -> RGB555: (sum + area/2) / area without a division per value
 * (the GBA has none): a reciprocal estimate, then corrected to the exact
 * quotient. */
static JPEG_FAST void finish(struct State *s,uint16_t *out){
 uint32_t area=s->w*s->hgt,half=area/2,inv=(uint32_t)(0xffffffffu/area);
 for(unsigned y=0;y<s->gh;y++)for(unsigned x=0;x<s->gw;x++){
  unsigned i=(s->oy+y)*G+s->ox+x;unsigned v[3];
  for(unsigned k=0;k<3;k++){uint32_t n=s->acc[i][k]+half,q=(uint32_t)(((uint64_t)n*inv)>>32);
   while(n-q*area>=area)q++;
   v[k]=q;}
  out[i]=(uint16_t)((v[0]>>3)|(v[1]>>3)<<5|(v[2]>>3)<<10);
 }
}
static int segment(struct State *s,int m,unsigned len){
 if(m==0xdb){                                   /* DQT */
  while(len){int pt=byte(s);if(pt<0)return JPEG_ERR_FORMAT;unsigned p=(unsigned)pt>>4,t=(unsigned)pt&15,n=p?129:65;
   if(p>1||t>3||len<n)return JPEG_ERR_FORMAT;
   for(unsigned k=0;k<64;k++){int a=byte(s),v=a;if(p){int b=byte(s);v=a<<8|b;if(b<0)v=-1;}
    if(v<=0)return JPEG_ERR_FORMAT;
    s->q[t][k]=(uint16_t)v;}
   s->qset|=1u<<t;
   len-=n;}
  return JPEG_OK;
 }
 if(m==0xc4){                                   /* DHT */
  while(len){if(len<17)return JPEG_ERR_FORMAT;int ct=byte(s);if(ct<0)return JPEG_ERR_FORMAT;
   unsigned c=(unsigned)ct>>4,t=(unsigned)ct&15,total=0;uint8_t counts[16];
   if(c>1||t>3)return JPEG_ERR_FORMAT;
   for(unsigned i=0;i<16;i++){int n=byte(s);if(n<0)return JPEG_ERR_FORMAT;counts[i]=(uint8_t)n;total+=(unsigned)n;}
   if(total>256||len<17+total)return JPEG_ERR_FORMAT;
   struct Huff *h=&s->h[c*4+t];
   for(unsigned i=0;i<total;i++){int v=byte(s);if(v<0)return JPEG_ERR_FORMAT;h->vals[i]=(uint8_t)v;}
   if(build(h,counts,total))return JPEG_ERR_FORMAT;
   len-=17+total;}
  return JPEG_OK;
 }
 if(m==0xc0||m==0xc1){                          /* baseline / extended sequential, Huffman */
  if(s->frame||len<6)return JPEG_ERR_FORMAT;
  int p=byte(s),hh=u16(s),ww=u16(s),n=byte(s);
  if(p<0||hh<0||ww<0||n<0)return JPEG_ERR_FORMAT;
  if(p!=8||!hh||!ww)return JPEG_ERR_UNSUPPORTED;
  if((unsigned)ww>JPEG_MAX_SIDE||(unsigned)hh>JPEG_MAX_SIDE)return JPEG_ERR_SIZE;
  if(n!=1&&n!=3)return JPEG_ERR_UNSUPPORTED;
  if(len!=6+3u*(unsigned)n)return JPEG_ERR_FORMAT;
  s->w=(unsigned)ww;s->hgt=(unsigned)hh;s->nc=(unsigned)n;s->hmax=s->vmax=1;
  for(unsigned i=0;i<s->nc;i++){int id=byte(s),hv=byte(s),tq=byte(s);if(tq<0||hv<0||id<0)return JPEG_ERR_FORMAT;
   struct Comp *c=&s->c[i];c->id=(uint8_t)id;c->h=(uint8_t)(hv>>4);c->v=(uint8_t)(hv&15);c->tq=(uint8_t)tq;
   if(c->h<1||c->h>2||c->v<1||c->v>2)return JPEG_ERR_UNSUPPORTED;
   if(tq>3)return JPEG_ERR_FORMAT;
   if(c->h>s->hmax)s->hmax=c->h;
   if(c->v>s->vmax)s->vmax=c->v;}
  if(s->nc==1){s->c[0].h=s->c[0].v=1;s->hmax=s->vmax=1;}   /* one component: never interleaved */
  if(s->nc==3&&s->c[0].id=='R'&&s->c[1].id=='G'&&s->c[2].id=='B')s->rgb=1;
  s->frame=1;return JPEG_OK;
 }
 if(m==0xdd){if(len!=2)return JPEG_ERR_FORMAT;int r=u16(s);if(r<0)return JPEG_ERR_FORMAT;s->restart=(unsigned)r;return JPEG_OK;}
 if(m==0xee&&len>=12){                          /* Adobe: transform 0 = RGB */
  uint8_t a[12];for(unsigned i=0;i<12;i++){int v=byte(s);if(v<0)return JPEG_ERR_FORMAT;a[i]=(uint8_t)v;}
  if(!memcmp(a,"Adobe",5)&&a[11]==0)s->rgb=1;
  return skip(s,len-12)?JPEG_ERR_FORMAT:JPEG_OK;
 }
 /* Progressive, lossless, hierarchical, arithmetic (and its DAC tables). */
 if(m>=0xc2&&m<=0xcf&&m!=0xc4&&m!=0xc8)return JPEG_ERR_UNSUPPORTED;
 return skip(s,len)?JPEG_ERR_FORMAT:JPEG_OK;
}
int jpeg_dc_cover(JpegRead read,JpegStop stop,void *ctx,uint32_t bytes,void *work,uint16_t out[JPEG_SIDE*JPEG_SIDE]){
 struct State *s=&jpeg_state;
#ifdef __arm__
 decoder_use(DECODER_JPEG);
#endif
 memset(s,0,sizeof(*s));memset(jpeg_huff,0,sizeof(jpeg_huff));memset(work,0,G*G*3*4);
 s->acc=work;s->h=jpeg_huff;s->read=read;s->stop=stop;s->ctx=ctx;s->size=bytes;
 memset(out,0,G*G*2);
 /* Slots of the coefficients kept: horizontal u and vertical v in 0,1,3,5,7. */
 static const uint8_t natural[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
 static const uint8_t slot8[8]={0,1,255,2,255,3,255,4};
 for(unsigned k=0;k<64;k++){unsigned u=slot8[natural[k]&7],v=slot8[natural[k]>>3];s->zslot[k]=(uint8_t)(u==255||v==255?255:u*5+v);}
 if(bytes>JPEG_MAX_BYTES)return JPEG_ERR_SIZE;
 if(byte(s)!=0xff||byte(s)!=0xd8)return s->err?s->err:JPEG_ERR_FORMAT;
 for(;;){
  int c=byte(s);if(c<0)return s->err?s->err:JPEG_ERR_FORMAT;
  if(c!=0xff)return JPEG_ERR_FORMAT;
  while(c==0xff)c=byte(s);
  if(c<0)return s->err?s->err:JPEG_ERR_FORMAT;
  if(c==0xd8||c==0xd9)return JPEG_ERR_FORMAT;
  if(c==0x01||(c>=0xd0&&c<=0xd7))continue;
  int len=u16(s);if(len<2)return s->err?s->err:JPEG_ERR_FORMAT;
  if(c!=0xda){int e=segment(s,c,(unsigned)len-2);if(e)return s->err?s->err:e;continue;}
  /* SOS: one interleaved scan with every component, full spectral range. */
  if(!s->frame)return JPEG_ERR_FORMAT;
  int n=byte(s);if(n<0)return JPEG_ERR_FORMAT;
  if((unsigned)n!=s->nc)return JPEG_ERR_UNSUPPORTED;
  if((unsigned)len!=6+2u*s->nc)return JPEG_ERR_FORMAT;
  unsigned used=0;
  for(unsigned i=0;i<s->nc;i++){int id=byte(s),t=byte(s);if(id<0||t<0)return JPEG_ERR_FORMAT;
   unsigned k=0;while(k<s->nc&&s->c[k].id!=id)k++;
   if(k==s->nc||(used>>k&1))return JPEG_ERR_FORMAT;
   used|=1u<<k;
   s->c[k].td=(uint8_t)(t>>4);s->c[k].ta=(uint8_t)(t&15);
   if(s->c[k].td>3||s->c[k].ta>3||!s->h[s->c[k].td].ok||!s->h[4+s->c[k].ta].ok||!(s->qset>>s->c[k].tq&1))return JPEG_ERR_FORMAT;}
  int ss=byte(s),se=byte(s),a=byte(s);
  if(ss<0||se<0||a<0)return JPEG_ERR_FORMAT;
  if(ss!=0||se!=63||a!=0)return JPEG_ERR_UNSUPPORTED;
  break;
 }
 /* Grid: the longer side G cells, the other rounded, centred. */
 if(s->w>=s->hgt){s->gw=G;s->gh=(G*s->hgt+s->w/2)/s->w;if(!s->gh)s->gh=1;}
 else{s->gh=G;s->gw=(G*s->w+s->hgt/2)/s->hgt;if(!s->gw)s->gw=1;}
 s->ox=(G-s->gw)/2;s->oy=(G-s->gh)/2;
 /* Units: block quarters (4 pixels), or whole blocks (8, the DC only)
  * when a cell spans 16 pixels or more (pictures of 1024+ pixels): the
  * quarters then change the cover little and cost 4x the work. */
 s->us=s->w>=16*s->gw&&s->hgt>=16*s->gh?3:2;
 unsigned u=1u<<s->us;
 for(unsigned b=0;b*u<s->w;b++){unsigned x0=b*u*s->gw,x1=(b*u+u<s->w?b*u+u:s->w)*s->gw,c=x0/s->w;s->cx[b]=(uint8_t)c;s->wx[b]=(uint16_t)((c+1)*s->w<x1?(c+1)*s->w-x0:x1-x0);}
 for(unsigned b=0;b*u<s->hgt;b++){unsigned y0=b*u*s->gh,y1=(b*u+u<s->hgt?b*u+u:s->hgt)*s->gh,c=y0/s->hgt;s->cy[b]=(uint8_t)c;s->wy[b]=(uint16_t)((c+1)*s->hgt<y1?(c+1)*s->hgt-y0:y1-y0);}
 s->big=s->w>=u*s->gw&&s->hgt>=u*s->gh;
 for(unsigned i=0;i<s->nc;i++)for(unsigned uy=0;uy<(8*s->vmax>>s->us);uy++)for(unsigned ux=0;ux<(8*s->hmax>>s->us);ux++){
  unsigned qx=(ux<<s->us)>>2,qy=(uy<<s->us)>>2,cu=qx*s->c[i].h/s->hmax,cv=qy*s->c[i].v/s->vmax;
  s->sub[i][uy*4+ux]=(uint8_t)(((cv>>1)*2+(cu>>1))*4+(cv&1)*2+(cu&1));}
 int e=scan(s);if(e)return e;
 finish(s,out);
 return JPEG_OK;
}

/* v2.1: a 64x64 RGB555 picture reduced to at most COVER_COLORS colours
 * (median cut): the box of pixels with the largest count x widest channel
 * range is split at the median of that channel (pixels of one value never
 * split), until COVER_COLORS boxes or every box is one colour. Each box's
 * colour is its rounded mean. Integer only and fully determined (ties: the
 * first box, the first channel R, G, B): tests/cover_quant.py is the same.
 * A split partitions the box in place (the boxes are the same pixel sets as
 * with a sort, so the result is too). ARM in the cover overlay; px in its
 * IWRAM scratch, the box table in the caller's work area; rec: COVER_REC
 * bytes. */
/* A box: its pixels px[start, start+count), its widest channel (axis) and
 * score = count x that channel's range (0: one colour, never split). */
struct QBox {uint16_t start,count;uint32_t score;uint32_t axis;};
/* Sets the box's axis and score; the minimum and maximum stay in registers
 * (one pass). */
static JPEG_FAST void qbox_scan(const uint32_t *p,unsigned start,unsigned n,struct QBox *b){
 unsigned lr=31,lg=31,lb=31,hr=0,hg=0,hb=0;
 for(unsigned i=0;i<n;i++){uint32_t v=p[i];unsigned r=v&31,g=(v>>5)&31,c=(v>>10)&31;
  lr=r<lr?r:lr;hr=r>hr?r:hr;lg=g<lg?g:lg;hg=g>hg?g:hg;lb=c<lb?c:lb;hb=c>hb?c:hb;}
 unsigned w=0,a=0;if(n){if(hr-lr>w){w=hr-lr;a=0;}if(hg-lg>w){w=hg-lg;a=1;}if(hb-lb>w){w=hb-lb;a=2;}}
 b->start=(uint16_t)start;b->count=(uint16_t)n;b->axis=a;b->score=n*w;
}
static JPEG_FAST void cover_quantize(const uint16_t *img,uint8_t *rec,uint32_t *px,struct QBox *box){
 for(unsigned i=0;i<COVER_PIXELS;i++)px[i]=(img[i]&0x7fffu)|(uint32_t)i<<16;
 unsigned nb=1;qbox_scan(px,0,COVER_PIXELS,&box[0]);
 while(nb<COVER_COLORS){
  unsigned best=nb;uint32_t score=0;
  for(unsigned b=0;b<nb;b++)if(box[b].score>score){score=box[b].score;best=b;}
  if(best==nb)break;
  struct QBox *b=&box[best];unsigned hist[32],n=b->count,start=b->start,sh=5*b->axis;uint32_t *p=px+start;
  for(unsigned v=0;v<32;v++)hist[v]=0;
  for(unsigned i=0;i<n;i++)hist[(p[i]>>sh)&31]++;
  unsigned cum=0,split=0,top=0;   /* values <= top go first */
  for(unsigned v=0;v<32;v++){cum+=hist[v];if(cum*2>=n){split=cum;top=v;if(split==n){split-=hist[v];top=v-1;}break;}}
  int i=0,j=(int)n-1;
  for(;;){while(i<=j&&((p[i]>>sh)&31)<=top)i++;while(i<=j&&((p[j]>>sh)&31)>top)j--;if(i>=j)break;uint32_t t=p[i];p[i]=p[j];p[j]=t;i++;j--;}
  qbox_scan(p,start,split,b);qbox_scan(p+split,start+split,n-split,&box[nb++]);
 }
 memset(rec+COVER_PAL,0,COVER_REC-COVER_PAL);
 for(unsigned b=0;b<nb;b++){
  uint32_t sr=0,sg=0,sb=0,n=box[b].count;const uint32_t *p=px+box[b].start;
  for(unsigned i=0;i<n;i++){uint32_t v=p[i];sr+=v&31;sg+=(v>>5)&31;sb+=(v>>10)&31;rec[v>>16]=(uint8_t)b;}
  unsigned rgb=(sr+n/2)/n|((sg+n/2)/n)<<5|((sb+n/2)/n)<<10;
  rec[COVER_PAL+2*b]=(uint8_t)rgb;rec[COVER_PAL+2*b+1]=(uint8_t)(rgb>>8);
 }
}
void jpeg_cover_reduce(const uint16_t *img,uint8_t *rec,void *work){
#ifdef __arm__
 decoder_use(DECODER_JPEG);
#endif
 cover_quantize(img,rec,jpeg_mem.px,(struct QBox*)work);
}
