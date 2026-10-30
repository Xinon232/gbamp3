/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Optional desktop-only GAC v4 kernels.
 * No global mutable state; no fast-math or architecture-specific ISA required.
 * Unsigned wrapping and explicit floor division avoid signed overflow/shift UB. */
#include <stdint.h>
#include <math.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
/* Shared immutable tables, unchanged from the player's checked-in format. */
#include "../player/src/gac4_tables.h"
static const uint16_t lut[4][1024]={GAC4_LUT0,GAC4_LUT1,GAC4_LUT2,GAC4_LUT3};
static const uint16_t header_lut[1024]=GAC4_HEADER_LUT;
static const int32_t steps[64]=GAC4_STEPS;
static const int16_t reflect[256]=GAC4_REFLECT;
static int clamp(int v, int lo, int hi) { return v<lo?lo:v>hi?hi:v; }
/* Floor division, independent of a compiler's signed right-shift behavior. */
static int32_t shift(int64_t v, unsigned n) {
    return (int32_t)(v>=0?v/(1LL<<n):-((-v+(1LL<<n)-1)/(1LL<<n)));
}
typedef struct { const uint8_t *b; unsigned pos; } Reader;
static unsigned peek(Reader *r, unsigned n) {
    unsigned byte=r->pos/8, v=0;
    for (unsigned i=0;i<3 && byte+i<1024;i++) v|=(unsigned)r->b[byte+i]<<(8*i);
    return (v>>(r->pos%8))&((1u<<n)-1);
}
static unsigned bits(Reader *r, unsigned n) { unsigned v=peek(r,n);r->pos+=n;return v; }
static unsigned code(Reader *r, const uint16_t *t) {
    unsigned v=t[peek(r,10)];r->pos+=v>>8;return v&255;
}
static int raw12(Reader *r) { unsigned v=bits(r,12);return (int)v-(int)((v&2048)<<1); }
typedef struct { int c[8],h[8],order,idx; } Stream;
static void begin(Stream *s, unsigned order, const uint8_t *refl, const uint8_t *hist, unsigned idx) {
    s->order=(int)order;s->idx=clamp((int)idx,1,63);
    for (unsigned m=0;m<order;m++) {
        int a[8], k=reflect[refl[m]];
        for (unsigned i=0;i<m;i++) a[i]=clamp(s->c[i]-shift((int64_t)k*s->c[m-1-i]+16384,15),-32768,32767);
        a[m]=shift(k+4,3);
        for (unsigned i=0;i<=m;i++) s->c[i]=a[i];
        unsigned h=hist[2*m]|((unsigned)hist[2*m+1]<<8);
        s->h[m]=(int)h-(int)((h&32768)<<1);
    }
}
static void sub(Reader *r, Stream *s, int *out) {
    unsigned sym=code(r,header_lut), tab=0;
    int q[32]={0};
    if (sym>=53) {tab=sym-53;s->idx=clamp((int)bits(r,6),1,63);}
    else if (sym!=52) {tab=sym%4;s->idx=clamp(s->idx+(int)(sym/4)-6,1,63);}
    if (sym!=52) for (unsigned i=0;i<32;) {
        unsigned v=code(r,lut[tab]);
        if (tab<2) {
            if (v==0x88) {q[i]=raw12(r);q[i+1]=raw12(r);}
            else {q[i]=(int)(v>>4)-(int)((v&128)>>3);q[i+1]=(int)(v&15)-(int)((v&8)<<1);}
            i+=2;
        } else {q[i++]=v==128?raw12(r):(int)v-(int)((v&128)<<1);}
    }
    for (int i=0;i<32;i++) {
        uint32_t acc=2048;
        for (int k=0;k<s->order;k++) acc+=(uint32_t)((int64_t)s->c[k]*s->h[k]);
        int64_t signed_acc=acc>2147483647u?(int64_t)acc-4294967296LL:acc;
        int x=clamp(shift(signed_acc,12)+q[i]*steps[s->idx],-32768,32767);
        for (int k=s->order-1;k>0;k--) s->h[k]=s->h[k-1];
        if (s->order) s->h[0]=x;
        out[i]=x;
    }
}
/* Returns unclipped signed32 PCM, preserving the Python reference's stereo sums.
 * Caller supplies exactly 1024 input bytes and frames*(mono?1:2) output slots. */
API int gac_decode(const uint8_t *b, int channels, int frames, int mono, int32_t *out) {
    if (!b || !out || (channels!=1 && channels!=2) || frames<1 || frames>8192) return 0;
    unsigned om=(unsigned)clamp(b[2]&15,0,8), so=(unsigned)clamp(b[2]>>4,0,8);
    int side=channels==2 && !(b[3]&1);
    if (!side) so=0;
    unsigned pos=6+((om+so+1)/2)*2;
    Stream m={0},s={0};
    begin(&m,om,b+6,b+pos,b[4]);begin(&s,so,b+6+om,b+pos+2*om,b[5]);
    Reader r={b,(pos+2*(om+so))*8};
    int prev=so?s.h[0]:0;
    for (int a=0;a<frames;a+=64) {
        int mid[64],sd[32]={0};
        sub(&r,&m,mid);sub(&r,&m,mid+32);
        if (side) sub(&r,&s,sd);
        for (int i=0;i<64 && a+i<frames;i++) {
            if (mono || channels==1) out[a+i]=mid[i];
            else {
                int v=sd[i/2],d=(i&1)?v:shift(prev+v,1);
                if (i&1) prev=v;
                out[2*(a+i)]=mid[i]+d;out[2*(a+i)+1]=mid[i]-d;
            }
        }
    }
    return 1;
}

API int gac_native_abi(void) { return 1; }
API void gac_inner(int64_t n, int64_t k, const double *tf, const int64_t *c,
                   const double *step, double dz, double *hk, double *qk) {
    for (int64_t s=0;s<n;s++) for (int64_t v=0;v<k;v++) {
        double *h=hk+(s*k+v)*40, *q=qk+(s*k+v)*32;
        double st=step[s*k+v];
        for (int j=0;j<32;j++) {
            int64_t sum=2048;
            for (int z=0;z<8;z++) sum+=(int64_t)h[j+z]*c[s*8+7-z];
            uint32_t bits=(uint32_t)sum;
            int64_t wrapped=bits>2147483647u?(int64_t)bits-4294967296LL:(int64_t)bits;
            double pred=floor((double)wrapped/4096.0);
            double val=(tf[s*32+j]-pred)/st;
            double quant=copysign(floor(fabs(val)+dz),val);
            if (quant < -2047.0) quant=-2047.0;
            if (quant > 2047.0) quant=2047.0;
            double x=pred+quant*st;
            if (x < -32768.0) x=-32768.0;
            if (x > 32767.0) x=32767.0;
            q[j]=quant; h[8+j]=x;
        }
    }
}
