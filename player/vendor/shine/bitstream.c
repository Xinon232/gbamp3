/*
 *  bit_stream.c package
 *  Author:  Jean-Georges Fritsch, C-Cube Microsystems
 *
 * This package provides functions to write information to the bit stream.
 *
 * Removed unused functions. Feb 2001 P.Everett
 */

#include "bitstream.h"
#include "types.h"


/* open the device to write the bit stream into it */
/* 2026-10-07 (gbamp3): the caller's buffer (no malloc); a frame never
 * comes near BUFFER_SIZE bytes (data_position restarts every frame). */
void shine_open_bit_stream(bitstream_t *bs, unsigned char *data, int size) {
  bs->data = data;
  bs->data_size = size;
  bs->data_position = 0;
  bs->cache = 0;
  bs->cache_bits = 32;
}

/*close the device containing the bit stream */
void shine_close_bit_stream(bitstream_t *bs) { bs->data = 0; }

/*
 * shine_putbits:
 * --------
 * write N bits into the bit stream.
 * bs = bit stream structure
 * val = value to write into the buffer
 * N = number of bits of val
 */
void shine_putbits(bitstream_t *bs, unsigned int val, unsigned int N) {
#ifdef DEBUG
  if (N > 32)
    printf("Cannot write more than 32 bits at a time.\n");
  if (N < 32 && (val >> N) != 0)
    printf("Upper bits (higher than %d) are not all zeros.\n", N);
#endif

  if (!N)
    return; /* 2026-10-07 (gbamp3): nothing to write (was a shift by 32) */
  if (bs->cache_bits > N) {
    bs->cache_bits -= N;
    bs->cache |= val << bs->cache_bits;
  } else {
    if (bs->data_position + sizeof(unsigned int) >= (unsigned)bs->data_size)
      return; /* 2026-10-07 (gbamp3): cannot happen (see shine_open_bit_stream) */

    N -= bs->cache_bits;
    bs->cache |= val >> N;
#ifdef SHINE_BIG_ENDIAN
    *(unsigned int *)(bs->data + bs->data_position) = bs->cache;
#else
    {unsigned c_ = bs->cache; unsigned char *d_ = bs->data + bs->data_position;   /* 2026-10-07 (gbamp3): bytes (no unaligned word store on ARM7) */
     d_[0] = (unsigned char)(c_ >> 24); d_[1] = (unsigned char)(c_ >> 16); d_[2] = (unsigned char)(c_ >> 8); d_[3] = (unsigned char)c_;}
#endif
    bs->data_position += sizeof(unsigned int);
    bs->cache_bits = 32 - N;
    if (N != 0)
      bs->cache = val << bs->cache_bits;
    else
      bs->cache = 0;
  }
}

int shine_get_bits_count(bitstream_t *bs) {
  return bs->data_position * 8 + 32 - bs->cache_bits;
}
