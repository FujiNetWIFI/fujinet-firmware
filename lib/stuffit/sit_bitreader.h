/*
 * sit_bitreader.h - MSB-first ("big-endian bit order") bitstream reader
 * over a sit_io byte source.
 *
 * Used by the classic-format decompressors (methods 1/2/3/13). Bits are
 * pulled from a 32-bit accumulator filled MSB-first from successive
 * input bytes in stream order (byte 0's bit 7 comes out first) -
 * equivalent to CSInputBuffer.m's CSInputNextBitString big-endian bit
 * order. Arsenic (method 15) uses its own arithmetic-coder bit source
 * in sit_arsenic.c and does not use this reader.
 *
 * Header-only (static inline) so it can be shared without a .o of its
 * own; no upstream file has a direct C equivalent.
 */
#ifndef FN_SIT_BITREADER_H
#define FN_SIT_BITREADER_H

#include <stdint.h>
#include "sit_io.h"

typedef struct {
    sit_io *io;
    uint32_t bitbuf;   /* bits left-justified: next bit is bit 31 */
    int nbits;          /* number of valid bits currently in bitbuf */
    int error;           /* set once the underlying byte source has run dry */
} sit_bitreader;

static inline void sit_br_init(sit_bitreader *br, sit_io *io)
{
    br->io = io;
    br->bitbuf = 0;
    br->nbits = 0;
    br->error = 0;
}

/* Top up the accumulator to at least 25 valid bits (unless the input
 * is exhausted, in which case zero bits are shifted in and br->error
 * is set - callers reading past the real end of a well-formed stream
 * only do so for the last, partial code, which decodes harmlessly). */
static inline void sit_br_fill(sit_bitreader *br)
{
    while (br->nbits <= 24) {
        int b = sit_io_getbyte(br->io);
        if (b < 0) {
            br->error = 1;
            b = 0;
        }
        br->bitbuf |= ((uint32_t)(uint8_t)b) << (24 - br->nbits);
        br->nbits += 8;
    }
}

static inline uint32_t sit_br_peekbits(sit_bitreader *br, int n)
{
    if (n <= 0) return 0;
    if (br->nbits < n) sit_br_fill(br);
    return br->bitbuf >> (32 - n);
}

static inline void sit_br_skipbits(sit_bitreader *br, int n)
{
    if (n <= 0) return;
    br->bitbuf <<= n;
    br->nbits -= n;
}

static inline uint32_t sit_br_getbits(sit_bitreader *br, int n)
{
    uint32_t v = sit_br_peekbits(br, n);
    sit_br_skipbits(br, n);
    return v;
}

/* Single-bit convenience, used heavily by prefix-code tree walks. */
static inline int sit_br_getbit(sit_bitreader *br)
{
    return (int)sit_br_getbits(br, 1);
}

/* Discard bits up to the next byte boundary (xadIOByteBoundary). */
static inline void sit_br_align(sit_bitreader *br)
{
    int rem = br->nbits & 7;
    if (rem) {
        br->bitbuf <<= rem;
        br->nbits -= rem;
    }
}

#endif /* FN_SIT_BITREADER_H */
