/* Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder). Volume Boost (v0.9.5): gain + stateless soft
 * limiter before the 8-bit output stage; same arithmetic as tools/boost.py.
 * On the GBA one copy of the 514-byte curve and of the limiter lives in the
 * common IWRAM region (defined by main.c with BOOST_DEFINE), used by the MP3
 * path and the GAC overlay alike; the limiter is inlined into each loop.
 * Only samples above the knee (-3 dBFS) take the limiter path. */
#pragma once
#include <stdint.h>
#include "boost_table.h"
static const uint16_t boost_gains[4]=BOOST_GAINS;
#if defined(__arm__) && !defined(BOOST_DEFINE)
extern const int16_t boost_curve[257];
#else
#ifdef __arm__
#define BOOST_CURVE_SCOPE
#define BOOST_SECTION __attribute__((section(".iwram.boost"),aligned(4)))
#else
#define BOOST_CURVE_SCOPE static
#define BOOST_SECTION
#endif
BOOST_CURVE_SCOPE const int16_t boost_curve[257] BOOST_SECTION=BOOST_CURVE;
#endif
/* Inline in both hot loops: a call would cost more than the limiter itself. */
static inline __attribute__((always_inline)) int boost_limit(int v){
 int a=v<0?-v:v;
 unsigned u=(unsigned)(a-BOOST_KNEE),i=u>>10;
 int y=i>=256?boost_curve[256]:boost_curve[i]+(((boost_curve[i+1]-boost_curve[i])*(int)(u&1023))>>10);
 y+=BOOST_KNEE;
 return v<0?-y:y;
}
/* g: Q8 gain, 256 = 0 dB (callers skip the call then). */
static inline __attribute__((always_inline)) int boost_apply(int x,int g){
 int v=(x*g)>>8;
 return (unsigned)(v+BOOST_KNEE)>2u*BOOST_KNEE?boost_limit(v):v;
}
