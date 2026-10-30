/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). GAC v4 encoder, stereo layout. See gac_enc.h and
 * tools/gac4.py (the reference: same block layout, unit order, header and
 * residual codes, candidate step offsets, rate-distortion slope). */
#include "gac_enc.h"
#include "gac4_tables.h"
/* On the GBA this file runs from the conversion overlay (IWRAM): the tables
 * used per sample go there too, the work arrays after the code. */
#if defined(__arm__)
#define GE_HOT __attribute__((section(".iwram.getab")))
#define GE_SCRATCH __attribute__((section(".conv_scratch")))
#define GE_FAST __attribute__((section(".iwram.gefast"),noinline))
#else
#define GE_HOT
#define GE_SCRATCH
#define GE_FAST
#endif
#include "gac_enc_tables.h"
#include <string.h>

#define UNIT 64u
#define SUB 32u
#define MAXO 8u
#define MAX_UNITS 128u
#define DELTA 6
#define ZERO_SYM 52u
#define ESC_SYM 53u
#define QMAX 2047
#define PAIR 7
#define SYMBOLS 24
#define LAMBDA 9000u          /* Params.lam (stereo layout) */
#define FLOOR 14              /* Params.floor */
#define SIDE_WEIGHT 2u
#define LOOK_MID 2048u
#define LOOK_SIDE 1024u
#define DZ ((uint32_t)(0.3*16777216.0))   /* 0.5 - Params.deadzone, Q24 */

static const int32_t STEP[64] GE_HOT=GAC4_STEPS;
static const int16_t REFL[256]=GAC4_REFLECT;


static int16_t cs_full[8+32] GE_SCRATCH,cs_q[32] GE_SCRATCH;   /* code_sub */
static struct GacEncSub cu_sub[3] GE_SCRATCH;   /* code_unit */
static int16_t ge_raw[2*GE_RAW_RING] GE_SCRATCH;   /* gac_enc_push: e->raw while pushing, twice in a row */
static int32_t cs_w[8+32] GE_SCRATCH;
static double lp_R[9] GE_SCRATCH,lp_a[8] GE_SCRATCH,lp_t[8] GE_SCRATCH,lp_k[8] GE_SCRATCH,lp_G[9*9] GE_SCRATCH;   /* lpc */
static int64_t lp_rw[9] GE_SCRATCH,lp_ru[9] GE_SCRATCH;
static int32_t lp_x[8+2048] GE_SCRATCH;   /* the look-ahead and the 8 samples before it, in a row */
#if defined(__arm__)
static uint32_t now(void){uint32_t h,l,h2;do{h=*(volatile uint16_t*)0x400010c;l=*(volatile uint16_t*)0x4000108;h2=*(volatile uint16_t*)0x400010c;}while(h!=h2);return h<<16|l;}
#else
static uint32_t now(void){return 0;}
#endif
/* ---------------------------------------------------------------- helpers */
static int16_t clamp16(int32_t v){return (int16_t)(v<-32768?-32768:v>32767?32767:v);}
static unsigned msb64(uint64_t v){unsigned n=0;if(v>>32){v>>=32;n=32;}if(v>>16){v>>=16;n+=16;}if(v>>8){v>>=8;n+=8;}if(v>>4){v>>=4;n+=4;}if(v>>2){v>>=2;n+=2;}if(v>>1)n++;return n;}
/* log2(v) in Q(fb) for v >= 1 (fb <= 16): integer part, then fb bits by squaring. */
static GE_FAST int32_t log2q(uint64_t v,unsigned fb){
 if(!v)return -(64<<fb);
 unsigned e=msb64(v);uint32_t m=(uint32_t)(e>=31?v>>(e-31):v<<(31-e));   /* [2^31, 2^32) */
 int32_t r=(int32_t)e<<fb;
 for(unsigned i=fb;i-->0;){uint64_t s=(uint64_t)m*m;if(s>>63){r|=1<<i;m=(uint32_t)(s>>32);}else m=(uint32_t)(s>>31);}
 return r;
}
/* Reflection bytes -> Q12 direct-form coefficients (gac4.step_up; the decoder's). */
static void step_up(const uint8_t *codes,unsigned n,int16_t *a){
 int16_t t[MAXO];
 for(unsigned j=0;j<n;j++){int32_t k=REFL[codes[j]];
  for(unsigned i=0;i<j;i++)t[i]=clamp16(a[i]-((k*a[j-1-i]+16384)>>15));
  for(unsigned i=0;i<j;i++)a[i]=t[i];
  a[j]=(int16_t)((k+4)>>3);}
}
/* LSB-first bit writer into a zeroed buffer; n <= 24. */
static GE_FAST void put(uint8_t *b,uint32_t *pos,uint32_t v,unsigned n){
 uint32_t p=*pos,x=(v&((1u<<n)-1u))<<(p&7);uint8_t *d=b+(p>>3);
 *pos=p+n;n+=p&7;
 d[0]|=(uint8_t)x;if(n>8){d[1]=(uint8_t)(x>>8);if(n>16){d[2]=(uint8_t)(x>>16);if(n>24)d[3]=(uint8_t)(x>>24);}}
}

/* ------------------------------------------------------------------ LPC */
/* r[k] = sum over i < n of x[i] x[i-k], k = 0..8 (x[-8..-1] are read). */
#if defined(__arm__)&&!defined(GE_NO_ASM)
void ge_lags(const int32_t *x,unsigned n,int64_t *r);
#else
static void ge_lags(const int32_t *x,unsigned n,int64_t *r){
 for(unsigned k=0;k<=MAXO;k++){int64_t s=0;for(unsigned i=0;i<n;i++)s+=(int64_t)x[i]*x[(int32_t)i-(int32_t)k];r[k]=s;}
}
#endif
/* Order, reflection bytes and coefficients of one stream for a block that
 * starts at sample `at` of `ring` (mask): Levinson-Durbin on the
 * Hann-windowed look-ahead of n samples (n = 2048 or 1024) with a small lag
 * window; the order (1, 2, 4, 6, 8) with the lowest estimated cost
 * n/2 log2(E/n + 1) + 24 order, E the open-loop residual energy of the
 * (unwindowed) look-ahead, from its covariance matrix. */
static GE_FAST void lpc(struct GacEnc *e_,const int16_t *ring,uint32_t mask,uint32_t at,unsigned n,struct GacEncStream *st){
 uint32_t tq=now();
 int32_t *x=lp_x+MAXO;
 for(int32_t i=-(int32_t)MAXO;i<(int32_t)n;i++)x[i]=i+(int32_t)at<0?0:ring[((uint32_t)((int32_t)at+i))&mask];
 for(unsigned i=0;i<MAXO;i++)st->hist[i]=(int16_t)x[-1-(int32_t)i];
 /* Covariance of the look-ahead and its 8 predecessors: G[a][b] = sum over
  * i < n of x[i-a] x[i-b], from the lags and the samples at both ends. */
 int64_t *ru=lp_ru;ge_lags(x,n,ru);
 double *G=lp_G;
 for(unsigned a=0;a<=MAXO;a++)for(unsigned b=a;b<=MAXO;b++){unsigned L=b-a;int64_t g=ru[L];
  for(int32_t m=-(int32_t)a;m<0;m++)g+=(int64_t)x[m]*x[m-(int32_t)L];
  for(int32_t m=(int32_t)(n-a);m<(int32_t)n;m++)g-=(int64_t)x[m]*x[m-(int32_t)L];
  G[a*9+b]=G[b*9+a]=(double)g;}
 /* Autocorrelation of the Hann-windowed look-ahead (numpy hanning(n + 2)[1:-1]
  * from the 2048 one), Q4 samples: precision for nearly tonal signals. */
 for(unsigned i=0;i<n;i++){unsigned p=n==2048?i:n==1024?2*i+1:(unsigned)(((i+1u)*2049u)/(n+1u))-1u;
  x[i]=(x[i]*(int32_t)GE_HANN[p<1024?p:2047-p]+2048)>>12;}
 for(unsigned i=0;i<MAXO;i++)lp_x[i]=0;
 int64_t *rw=lp_rw;ge_lags(x,n,rw);
 e_->cyc_l[0]+=now()-tq;tq=now();
 /* Levinson-Durbin in double precision (soft float: a few hundred
  * operations per block; fixed point loses the small reflection
  * coefficients of nearly perfectly predicted, tonal signals). */
 double *ks=lp_k;for(unsigned i=0;i<MAXO;i++)ks[i]=0;
 if(rw[0]>0&&n>MAXO){
  double *R=lp_R;for(unsigned k=0;k<=MAXO;k++)R[k]=(double)rw[k];
  R[0]*=1.0+1e-6;
  for(unsigned k=1;k<=MAXO;k++)R[k]*=1.0-1.8e-5*(double)(k*k);       /* exp(-(0.006 k)^2 / 2) */
  double *a=lp_a,*t=lp_t,e=R[0];
  for(unsigned i=0;i<MAXO;i++){
   double acc=R[i+1];for(unsigned j=0;j<i;j++)acc-=a[j]*R[i-j];
   double k=acc/e;if(k>0.9999)k=0.9999;if(k<-0.9999)k=-0.9999;
   for(unsigned j=0;j<i;j++)t[j]=a[j]-k*a[i-1-j];
   for(unsigned j=0;j<i;j++)a[j]=t[j];
   a[i]=k;ks[i]=k;e*=1.0-k*k;
   if(e<=0)break;
  }
 }
 uint8_t codes[MAXO];
 for(unsigned i=0;i<MAXO;i++){double ak=ks[i]<0?-ks[i]:ks[i];uint32_t m=(uint32_t)(ak*1073741824.0);unsigned lo=0,hi=127;
  while(lo<hi){unsigned mid=(lo+hi)/2;if((uint32_t)GE_KMID[mid]<m)lo=mid+1;else hi=mid;}
  codes[i]=(uint8_t)(ks[i]<0?-(int)lo:(int)lo);}
 e_->cyc_l[1]+=now()-tq;tq=now();
 /* Order: the open-loop residual energy with the stored coefficients
  * (as tools/gac4.py _choose): d' G d / 2^24 with d = (4096, -c), plus
  * n/12 for the rounding of the prediction. */
 static const uint8_t orders[5]={1,2,4,6,8};int64_t best=0;unsigned bo=1;
 for(unsigned oi=0;oi<5;oi++){unsigned o=orders[oi];int16_t c[MAXO];step_up(codes,o,c);
  double d[MAXO+1];d[0]=4096.0;for(unsigned k=0;k<o;k++)d[k+1]=-(double)c[k];
  double D=0;
  for(unsigned a=0;a<=o;a++){double v=0;for(unsigned b=a+1;b<=o;b++)v+=d[b]*G[a*9+b];D+=d[a]*(d[a]*G[a*9+a]+2.0*v);}
  double Ef=D*(1.0/16777216.0)+(double)n*(1.0/12.0);
  uint64_t E=Ef>0?(uint64_t)Ef:0;
  int64_t cost=(int64_t)(n/2)*(log2q(E+n,12)-log2q(n,12))+((int64_t)24*o<<12);
  if(!oi||cost<best){best=cost;bo=o;}}
 e_->cyc_l[2]+=now()-tq;
 st->order=(uint8_t)bo;memcpy(st->refl,codes,MAXO);
 memset(st->c,0,sizeof(st->c));step_up(codes,bo,st->c);
 memcpy(st->h,st->hist,sizeof(st->h));st->first=1;
}

/* ------------------------------------------------------------ subblocks */
/* One step-size candidate of a subblock (layout used by gac_enc_arm.S):
 * closed loop over 32 samples; stops early (returns 0) once the weighted
 * distortion passes best, checked after every 4th sample; else returns 32. */
struct GeCand {const int16_t *t;int32_t *w;int16_t *q;const int32_t *c;int32_t step;uint32_t inv,shift,bestlo,besthi,dlo,dhi,live;};
#if defined(__arm__)&&!defined(GE_NO_ASM)
int ge_candidate_arm(struct GeCand *p);
#define ge_candidate ge_candidate_arm
#else
static int ge_candidate(struct GeCand *p){
 uint64_t d=0,best=(uint64_t)p->besthi<<32|p->bestlo;unsigned live=0;int32_t *w=p->w;
 for(unsigned j=0;j<SUB;j++){
  int32_t acc=2048;const int32_t *wp=w+MAXO-1+j;for(unsigned k=0;k<MAXO;k++)acc+=p->c[k]*wp[-(int)k];
  int32_t pj=acc>>12,dv=p->t[j]-pj;uint32_t a=(uint32_t)(dv<0?-dv:dv);
  uint32_t qq=(uint32_t)(((uint64_t)a*p->inv+DZ)>>24);if(qq>QMAX)qq=QMAX;
  int32_t qv=dv<0?-(int32_t)qq:(int32_t)qq;live|=qq;
  int32_t x=pj+qv*p->step;x=x<-32768?-32768:x>32767?32767:x;
  int32_t er=x-p->t[j];d+=(uint64_t)((int64_t)er*er);
  p->q[j]=(int16_t)qv;w[MAXO+j]=x;
  if((j&3)==3&&(d<<p->shift)>best){p->dlo=(uint32_t)d;p->dhi=(uint32_t)(d>>32);p->live=live;return 0;}
 }
 p->dlo=(uint32_t)d;p->dhi=(uint32_t)(d>>32);p->live=live;return 32;
}
#endif
static unsigned hbits(unsigned idx,unsigned cur,unsigned tab){
 if(!idx)return GE_HLEN[ZERO_SYM];
 int d=(int)idx-(int)cur;return d>=-DELTA&&d<=DELTA?GE_HLEN[(unsigned)(d+DELTA)*4+tab]:GE_HLEN[ESC_SYM+tab]+6u;
}
/* Bits of 32 residuals under each table. */
static void rbits(const int16_t *q,uint32_t *b){
 uint32_t b0=0,b1=0,b2=0,b3=0;
 for(unsigned i=0;i<SUB;i+=2){int a=q[i],c=q[i+1];
  if(a<-PAIR||a>PAIR||c<-PAIR||c>PAIR){b0+=GE_LEN0[225]+24u;b1+=GE_LEN1[225]+24u;}
  else{unsigned s=(unsigned)(a+PAIR)*15u+(unsigned)(c+PAIR);b0+=GE_LEN0[s];b1+=GE_LEN1[s];}
  for(unsigned k=0;k<2;k++){int v=q[i+k];
   if(v<-SYMBOLS||v>SYMBOLS){b2+=GE_LEN2[49]+12u;b3+=GE_LEN3[49]+12u;}
   else{b2+=GE_LEN2[v+SYMBOLS];b3+=GE_LEN3[v+SYMBOLS];}}
 }
 b[0]=b0;b[1]=b1;b[2]=b2;b[3]=b3;
}
/* One subblock: t targets, src the 8 source samples before them (newest
 * first), h the decoded history (newest first), c/o the predictor. */
static GE_FAST void code_sub(struct GacEnc *e,const int16_t *t,const int16_t *src,const int16_t *h,const int16_t *c16,unsigned o,unsigned cur,int first,unsigned weight,struct GacEncSub *r){
 int32_t c[MAXO];for(unsigned k=0;k<MAXO;k++)c[k]=c16[k];   /* off EWRAM */
 /* Open-loop step estimate from the source signal. */
 const uint32_t *inv_step=e->inv_step;int16_t *full=cs_full;for(unsigned k=0;k<MAXO;k++)full[k]=src[MAXO-1-k];memcpy(full+MAXO,t,sizeof(int16_t)*SUB);
 uint64_t ss=0;
 for(unsigned j=0;j<SUB;j++){int32_t acc=2048;const int16_t *fp=full+MAXO-1+j;for(unsigned k=0;k<o;k++)acc+=c[k]*fp[-(int)k];   /* 32 bits, as the decoder */
  int32_t e=t[j]-(acc>>12);ss+=(uint64_t)((int64_t)e*e);}
 /* base = round(4 log2(rms) - 7) = round(2 log2(ss / 32) - 7) */
 int base=ss?(2*log2q(ss,8)-17*256+128)>>8:-126;
 if(first)cur=(unsigned)(base<FLOOR?FLOOR:base>63?63:base);
 int32_t *w=cs_w;for(unsigned k=0;k<MAXO;k++)w[k]=h[MAXO-1-k];
 int16_t *q=cs_q;uint64_t bestc=~(uint64_t)0;int bestoff=99,tried[7],nt=0;
 /* Offsets from the smallest step up (the estimate runs high: the smallest
  * step wins most subblocks); a candidate stops once its distortion alone
  * costs more than the best so far. Ties go to the lower offset, as in
  * tools/gac4.py (argmin). */
 static const int8_t order_[2]={-6,-4};   /* -2..+6 win under 0.5% of subblocks (the estimate runs high); see V200_VERIFICATION.md */
 for(unsigned oi=0;oi<2;oi++){int off=order_[oi];
  int idx=base+off;idx=idx<FLOOR?FLOOR:idx>63?63:idx;
  int dup=0;for(int k=0;k<nt;k++)if(tried[k]==idx)dup=1;
  if(dup)continue;tried[nt++]=idx;
  struct GeCand p={t,w,q,c,STEP[idx],inv_step[idx],weight==2?1u:0u,(uint32_t)bestc,(uint32_t)(bestc>>32),0,0,0};
  e->evals++;
  uint32_t tc=now();int done_=ge_candidate(&p);e->cyc_close+=now()-tc;
  if(!done_)continue;   /* costs more than the best so far */
  uint64_t d=(uint64_t)p.dhi<<32|p.dlo;unsigned live=p.live;
  uint32_t tb[4],bits;unsigned tab=0;
  if(live){rbits(q,tb);for(unsigned k=1;k<4;k++)if(tb[k]<tb[tab])tab=k;bits=hbits((unsigned)idx,cur,tab)+tb[tab];}
  else bits=hbits(0,cur,0);
  uint64_t cost=d*weight+(uint64_t)LAMBDA*bits;
  if(cost<bestc||(cost==bestc&&off<bestoff)){bestc=cost;bestoff=off;r->idx=(uint8_t)(live?idx:0);r->tab=(uint8_t)tab;r->bits=bits;r->d=d;
   memcpy(r->q,q,sizeof(int16_t)*SUB);for(unsigned k=0;k<SUB;k++)r->x[k]=(int16_t)w[MAXO+k];}
 }
 r->cur=(uint8_t)cur;
 #ifdef GE_STATS
 {extern unsigned ge_hist[16];ge_hist[(bestoff+6)/2]++;}
 #endif
 for(unsigned k=0;k<MAXO;k++)r->newh[k]=r->x[SUB-1-k];
}
/* The codes of a coded subblock. */
static GE_FAST void write_sub(uint8_t *b,uint32_t *pos,const struct GacEncSub *r,unsigned cur){
 unsigned idx=r->idx,tab=r->tab;
 if(!idx){put(b,pos,GE_HCODE[ZERO_SYM],GE_HLEN[ZERO_SYM]);return;}
 int d=(int)idx-(int)cur;
 if(d>=-DELTA&&d<=DELTA){unsigned s=(unsigned)(d+DELTA)*4+tab;put(b,pos,GE_HCODE[s],GE_HLEN[s]);}
 else{put(b,pos,GE_HCODE[ESC_SYM+tab],GE_HLEN[ESC_SYM+tab]);put(b,pos,idx,6);}
 const int16_t *q=r->q;
 if(tab<2){const uint8_t *L=tab?GE_LEN1:GE_LEN0;const uint16_t *C=tab?GE_CODE1:GE_CODE0;
  for(unsigned i=0;i<SUB;i+=2){int a=q[i],c=q[i+1];
   if(a<-PAIR||a>PAIR||c<-PAIR||c>PAIR){put(b,pos,C[225],L[225]);put(b,pos,(uint32_t)a&0xfff,12);put(b,pos,(uint32_t)c&0xfff,12);}
   else{unsigned s=(unsigned)(a+PAIR)*15u+(unsigned)(c+PAIR);put(b,pos,C[s],L[s]);}}}
 else{const uint8_t *L=tab==3?GE_LEN3:GE_LEN2;const uint16_t *C=tab==3?GE_CODE3:GE_CODE2;
  for(unsigned i=0;i<SUB;i++){int v=q[i];
   if(v<-SYMBOLS||v>SYMBOLS){put(b,pos,C[49],L[49]);put(b,pos,(uint32_t)v&0xfff,12);}
   else put(b,pos,C[v+SYMBOLS],L[v+SYMBOLS]);}}
}

/* --------------------------------------------------------------- blocks */

static unsigned head_bytes(unsigned om,unsigned so){return 6u+(om+so+1u)/2u*2u+2u*(om+so);}
static void open_block(struct GacEnc *e){
 uint32_t u=e->unit;
 /* The look-ahead ends with the (zero padded) last unit, as in tools/gac4.py. */
 unsigned nm=LOOK_MID,ns=LOOK_SIDE;
 if(e->finishing){uint32_t left=e->units-u;if(left*UNIT<nm)nm=left*UNIT;if(left*(UNIT/2)<ns)ns=left*(UNIT/2);}
 uint32_t t0=now();lpc(e,e->mid,GE_MID_RING-1,u*UNIT,nm,&e->m);
 lpc(e,e->side,GE_SIDE_RING-1,u*(UNIT/2),ns,&e->s);e->cyc_lpc+=now()-t0;
 e->block_start=u;e->block_units=0;e->block_bits=0;
 e->block_cap=(1024u-head_bytes(e->m.order,e->s.order))*8u;
 e->block_open=1;e->side_peak=0;e->side_live=0;
 memset(e->bitbuf,0,sizeof(e->bitbuf));
}
static int close_block(struct GacEnc *e){
 uint32_t n=e->block_units,a=e->block_start;
 uint32_t frames=n*UNIT;if(e->finishing&&a*UNIT+frames>e->total)frames=e->total-a*UNIT;
 int silent=!e->side_live&&e->side_peak<64;
 unsigned om=e->m.order,so=silent?0:e->s.order;
 uint8_t *b=e->block;memset(b,0,1024);
 b[0]=(uint8_t)frames;b[1]=(uint8_t)(frames>>8);b[2]=(uint8_t)(om|so<<4);b[3]=(uint8_t)silent;b[4]=e->m.init;b[5]=silent?0:e->s.init;
 unsigned p=6;memcpy(b+p,e->m.refl,om);p+=om;memcpy(b+p,e->s.refl,so);p+=so;p=(p+1u)&~1u;
 for(unsigned k=0;k<om;k++){b[p++]=(uint8_t)e->m.hist[k];b[p++]=(uint8_t)((uint16_t)e->m.hist[k]>>8);}
 for(unsigned k=0;k<so;k++){b[p++]=(uint8_t)e->s.hist[k];b[p++]=(uint8_t)((uint16_t)e->s.hist[k]>>8);}
 uint32_t bp=p*8u;
 if(!silent){uint32_t nb=(e->block_bits+7u)/8u;memcpy(b+p,e->bitbuf,nb);bp+=e->block_bits;}
 else{   /* without the side codes (each the zero-subblock code) */
  unsigned zl=GE_HLEN[ZERO_SYM];uint32_t from=0;
  for(uint32_t i=0;i<=n;i++){uint32_t to=i<n?e->side_at[i]:e->block_bits;
   for(uint32_t k=from;k<to;k++)if(e->bitbuf[k>>3]>>(k&7)&1)b[bp>>3]|=(uint8_t)(1u<<(bp&7)),bp++;else bp++;
   from=to+zl;}
 }
 e->block_open=0;e->blocks++;
 return e->out(e->ctx,b,a*UNIT);
}
static int code_unit(struct GacEnc *e){
 uint32_t u=e->unit;struct GacEncSub *m0=&cu_sub[0],*m1=&cu_sub[1],*s0=&cu_sub[2];int16_t t[SUB],src[MAXO];
 #define MS(i) ((int32_t)(i)<0?0:e->mid[(uint32_t)(i)&(GE_MID_RING-1)])
 #define SS(i) ((int32_t)(i)<0?0:e->side[(uint32_t)(i)&(GE_SIDE_RING-1)])
 int32_t a=(int32_t)(u*UNIT);
 for(unsigned j=0;j<SUB;j++)t[j]=MS(a+(int32_t)j);for(unsigned k=0;k<MAXO;k++)src[k]=MS(a-1-(int32_t)k);
 uint32_t t0=now();code_sub(e,t,src,e->m.h,e->m.c,e->m.order,e->m.cur,e->m.first,1,m0);
 int32_t sa=(int32_t)(u*(UNIT/2));int16_t ts[SUB],srcs[MAXO];int32_t peak=0;
 for(unsigned j=0;j<SUB;j++){ts[j]=SS(sa+(int32_t)j);int32_t v=ts[j]<0?-ts[j]:ts[j];if(v>peak)peak=v;}
 for(unsigned k=0;k<MAXO;k++)srcs[k]=SS(sa-1-(int32_t)k);
 code_sub(e,ts,srcs,e->s.h,e->s.c,e->s.order,e->s.cur,e->s.first,SIDE_WEIGHT,s0);
 for(unsigned j=0;j<SUB;j++)t[j]=MS(a+(int32_t)(SUB+j));for(unsigned k=0;k<MAXO;k++)src[k]=MS(a+(int32_t)SUB-1-(int32_t)k);
 unsigned cur1=m0->idx?m0->idx:m0->cur;
 code_sub(e,t,src,m0->newh,e->m.c,e->m.order,cur1,0,1,m1);e->cyc_sub+=now()-t0;
 uint32_t bits=m0->bits+m1->bits+s0->bits;
 if(e->block_units&&e->block_bits+bits>e->block_cap){t0=now();int rr=close_block(e);e->cyc_close+=now()-t0;return rr;}   /* the unit starts the next block */
 if(e->m.first)e->m.init=m0->cur;
 if(e->s.first)e->s.init=s0->cur;
 write_sub(e->bitbuf,&e->block_bits,m0,m0->cur);
 write_sub(e->bitbuf,&e->block_bits,m1,cur1);
 e->side_at[e->block_units]=(uint16_t)e->block_bits;
 write_sub(e->bitbuf,&e->block_bits,s0,s0->cur);
 memcpy(e->m.h,m1->newh,sizeof(e->m.h));memcpy(e->s.h,s0->newh,sizeof(e->s.h));
 e->m.cur=m1->idx?m1->idx:cur1;e->s.cur=s0->idx?s0->idx:s0->cur;e->m.first=e->s.first=0;
 if(s0->idx)e->side_live=1;if(peak>e->side_peak)e->side_peak=peak;
 e->sq_err+=m0->d+m1->d+s0->d*2;
 e->block_units++;e->unit++;
 if(e->block_units==MAX_UNITS||(e->finishing&&e->unit>=e->units))return close_block(e);
 return 0;
 #undef MS
 #undef SS
}
static int run(struct GacEnc *e){
 for(;;){
  if(e->finishing&&e->unit>=e->units)return e->block_open?close_block(e):0;
  uint32_t u=e->unit;
  if(!e->block_open){if(e->pushed<u*UNIT+LOOK_MID||e->side_made<u*(UNIT/2)+LOOK_SIDE)return 0;open_block(e);}
  if(e->pushed<(u+1)*UNIT||e->side_made<(u+1)*(UNIT/2))return 0;
  int r=code_unit(e);if(r)return r;
 }
}

/* ----------------------------------------------------------------- API */
void gac_enc_begin(struct GacEnc *e,gac_enc_block_fn out,void *ctx){
 memset(e,0,sizeof(*e));e->out=out;e->ctx=ctx;
 /* From a ROM copy of the steps: gac_enc_begin may run while another
  * overlay occupies IWRAM (STEP lives in the conversion overlay). */
 static const int32_t step_rom[64]=GAC4_STEPS;
 for(unsigned i=1;i<64;i++)e->inv_step[i]=(uint32_t)((16777216u+(uint32_t)step_rom[i]/2u)/(uint32_t)step_rom[i]);
}
GE_FAST int gac_enc_push(struct GacEnc *e,const int16_t *lr,unsigned frames){
 int rv=0;
 int16_t *rw_=ge_raw;memcpy(rw_,e->raw,sizeof(e->raw));memcpy(rw_+GE_RAW_RING,e->raw,sizeof(e->raw));
 int16_t *mid=e->mid,*side=e->side;uint32_t f=e->pushed,k=e->side_made;
 for(unsigned i=0;i<frames;i++){
  int32_t L=lr?lr[2*i]:0,R=lr?lr[2*i+1]:0;
  mid[f&(GE_MID_RING-1)]=(int16_t)((L+R)>>1);
  int16_t sv=(int16_t)((L-R)>>1);rw_[f&(GE_RAW_RING-1)]=rw_[(f&(GE_RAW_RING-1))+GE_RAW_RING]=sv;f++;
  /* Side sample k is frame 2k+1 after the 31-tap lowpass (15 frames ahead). */
  if(2u*k+16u<f){int32_t acc=16384;
   /* symmetric taps; frames before 0 read the zeroed ring */
   const int16_t *x=rw_+((2*k+1u-15u)&(GE_RAW_RING-1));
   #pragma GCC unroll 15
   for(unsigned j=0;j<15;j++)acc+=GE_LP[j]*(x[j]+x[30-j]);
   acc+=GE_LP[15]*x[15];
   side[k&(GE_SIDE_RING-1)]=clamp16(acc>>15);k++;}
  if(!(f&31u)){e->pushed=f;e->side_made=k;int r=run(e);if(r){rv=r;break;}}
 }
 e->pushed=f;e->side_made=k;
 memcpy(e->raw,rw_,sizeof(e->raw));
 return rv;
}
int gac_enc_finish(struct GacEnc *e){
 if(!e->pushed)return -1;
 e->total=e->pushed;e->units=(e->total+UNIT-1)/UNIT;e->finishing=1;
 int r=run(e);
 while(!r&&e->unit<e->units){r=gac_enc_push(e,0,32);if(!r)r=run(e);}
 return r;
}
