#include <stdint.h>

#ifndef mul
#define mul(a, b) (int32_t)((((int64_t)a) * ((int64_t)b)) >> 32)
#endif

#ifndef muls
#define muls(a, b) (int32_t)((((int64_t)a) * ((int64_t)b)) >> 31)
#endif

#ifndef mulr
#define mulr(a, b)                                                             \
  (int32_t)(((((int64_t)a) * ((int64_t)b)) + 0x80000000LL) >> 32)
#endif

#ifndef mulsr
#define mulsr(a, b)                                                            \
  (int32_t)(((((int64_t)a) * ((int64_t)b)) + 0x40000000LL) >> 31)
#endif

#ifndef mul0
/* 2026-10-07 (gbamp3): a 64-bit accumulator in (hi, lo), as SMULL / SMLAL
 * (mult_sarm_gcc.h): the PC build gives the GBA's results bit for bit
 * (was: the sum of the products' upper halves). */
#define mul0(hi, lo, a, b)                                                     \
  do {                                                                         \
    int64_t t_ = (int64_t)(a) * (int64_t)(b);                                  \
    (hi) = (int32_t)(t_ >> 32);                                                \
    (lo) = (uint32_t)t_;                                                       \
  } while (0)
#define muladd(hi, lo, a, b)                                                   \
  do {                                                                         \
    int64_t t_ = (int64_t)(((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo)) + \
                 (int64_t)(a) * (int64_t)(b);                                  \
    (hi) = (int32_t)(t_ >> 32);                                                \
    (lo) = (uint32_t)t_;                                                       \
  } while (0)
#define mulsub(hi, lo, a, b) muladd(hi, lo, a, -(int64_t)(b))
#define mulz(hi, lo)
#endif

#ifndef cmuls
#define cmuls(dre, dim, are, aim, bre, bim)                                    \
  do {                                                                         \
    int32_t tre;                                                               \
    (tre) = (int32_t)(((int64_t)(are) * (int64_t)(bre) -                       \
                       (int64_t)(aim) * (int64_t)(bim)) >>                     \
                      31);                                                     \
    (dim) = (int32_t)(((int64_t)(are) * (int64_t)(bim) +                       \
                       (int64_t)(aim) * (int64_t)(bre)) >>                     \
                      31);                                                     \
    (dre) = tre;                                                               \
  } while (0)
#endif
