/*
 * sit_lzh13.c - LZ77 (64KB window) + adaptive/semi-adaptive Huffman
 * decompressor (StuffIt classic-format compression method 13, the
 * main classic-format compressor). Ported from XADStuffIt13Handle.m/.h
 * and its base class XADLZSSHandle.m/.h, plus the shared
 * sit_prefixcode.c/.h tree builder for the from-code-lengths
 * ("shortestCodeIsZeros:YES") path, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * IMPORTANT bit order: every CSInput* call in XADStuffIt13Handle.m is
 * either a "*LE" (low-bit-first) bit/bitstring/symbol read, or
 * CSInputNextByte (a single plain, non-bit-buffered byte read used
 * only once, for the leading control byte). Method 13 therefore reads
 * LSB-first throughout - DIFFERENT from the shared MSB-first
 * sit_bitreader.h used by method 3. This file implements its own
 * small self-contained LSB-first bit reader below (same idiom as
 * sit_lzw.c's, kept local rather than shared, per the porting notes
 * for method 2).
 *
 * Window handling is ported from XADLZSSHandle.m's produceByteAtOffset:
 * a 65536-byte circular window (windowSize:65536 in
 * XADStuffIt13Handle.m's initWithHandle:length:), addressed by a
 * monotonically increasing absolute byte position masked with
 * (65536-1); a literal byte is written into the window at the current
 * position, a match sets up (matchlength,matchoffset) and then falls
 * through to copy one byte per output position from
 * window[matchoffset++ & mask] - copying byte-by-byte (rather than
 * with memmove) is what makes overlapping matches (offset < length)
 * work correctly, and is preserved here.
 *
 * Verified against real data: see the sit_lzh13_decompress harness run
 * against mousepaintmanual.sit's method-13 entries (reported
 * separately) - cmp-identical to `unar`'s output for all five sampled
 * entries, from 80 bytes up to ~111KB.
 */
#include <string.h>
#include "sit_internal.h"
#include "sit_prefixcode.h"

/* ------------------------------------------------------------------ */
/* LSB-first ("LE") bit reader over sit_io, local to this file.        */
/* ------------------------------------------------------------------ */
typedef struct {
    sit_io *io;
    uint32_t bitbuf;
    int nbits;
    int error;
} lzh13_br;

static void lzh13_br_init(lzh13_br *br, sit_io *io)
{
    br->io = io;
    br->bitbuf = 0;
    br->nbits = 0;
    br->error = 0;
}

/* n must be <= 24 (the largest n used anywhere in this file is 15). */
static int lzh13_getbits(lzh13_br *br, int n)
{
    while (br->nbits < n) {
        int b = sit_io_getbyte(br->io);
        if (b < 0) { br->error = 1; return -1; }
        br->bitbuf |= ((uint32_t)(uint8_t)b) << br->nbits;
        br->nbits += 8;
    }
    uint32_t v = br->bitbuf & ((1u << n) - 1u);
    br->bitbuf >>= n;
    br->nbits -= n;
    return (int)v;
}

static int lzh13_getbit(lzh13_br *br)
{
    return lzh13_getbits(br, 1);
}

static int lzh13_bitfn(void *ctx)
{
    return lzh13_getbit((lzh13_br *)ctx);
}

/* ------------------------------------------------------------------ */
/* Static code tables (5 built-in tables selectable by the control     */
/* byte's high nibble), verbatim from XADStuffIt13Handle.m.            */
/* ------------------------------------------------------------------ */
#define SIT_LZH13_NUMCODES 321 /* 256 literals + 62 length codes + 2 long-length escapes + 1 end marker */

static const int FirstCodeLengths_1[321] = {
     4, 5, 7, 8, 8, 9, 9, 9, 9, 7, 9, 9, 9, 8, 9, 9,
     9, 9, 9, 9, 9, 9, 9,10, 9, 9,10,10, 9,10, 9, 9,
     5, 9, 9, 9, 9,10, 9, 9, 9, 9, 9, 9, 9, 9, 7, 9,
     9, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 8, 9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 7, 8, 9,
     7, 9, 9, 7, 7, 9, 9, 9, 9,10, 9,10,10,10, 9, 9,
     9, 5, 9, 8, 7, 5, 9, 8, 8, 7, 9, 9, 8, 8, 5, 5,
     7,10, 5, 8, 5, 8, 9, 9, 9, 9, 9,10, 9, 9,10, 9,
     9,10,10,10,10,10,10,10, 9,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10, 9, 9,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10, 9,10,10,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10, 9, 9,10,10,
     9,10,10,10,10,10,10,10, 9,10,10,10, 9,10, 9, 5,
     6, 5, 5, 8, 9, 9, 9, 9, 9, 9,10,10,10, 9,10,10,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10, 9,10, 9, 9, 9,10, 9,10, 9,10, 9,10, 9,
    10,10,10, 9,10, 9,10,10, 9, 9, 9, 6, 9, 9,10, 9,
     5,
};

static const int SecondCodeLengths_1[321] = {
     4, 5, 6, 6, 7, 7, 6, 7, 7, 7, 6, 8, 7, 8, 8, 8,
     8, 9, 6, 9, 8, 9, 8, 9, 9, 9, 8,10, 5, 9, 7, 9,
     6, 9, 8,10, 9,10, 8, 8, 9, 9, 7, 9, 8, 9, 8, 9,
     8, 8, 6, 9, 9, 8, 8, 9, 9,10, 8, 9, 9,10, 8,10,
     8, 8, 8, 8, 8, 9, 7,10, 6, 9, 9,11, 7, 8, 8, 9,
     8,10, 7, 8, 6, 9,10, 9, 9,10, 8,11, 9,11, 9,10,
     9, 8, 9, 8, 8, 8, 8,10, 9, 9,10,10, 8, 9, 8, 8,
     8,11, 9, 8, 8, 9, 9,10, 8,11,10,10, 8,10, 9,10,
     8, 9, 9,11, 9,11, 9,10,10,11,10,12, 9,12,10,11,
    10,11, 9,10,10,11,10,11,10,11,10,11,10,10,10, 9,
     9, 9, 8, 7, 6, 8,11,11, 9,12,10,12, 9,11,11,11,
    10,12,11,11,10,12,10,11,10,10,10,11,10,11,11,11,
     9,12,10,12,11,12,10,11,10,12,11,12,11,12,11,12,
    10,12,11,12,11,11,10,12,10,11,10,12,10,12,10,12,
    10,11,11,11,10,11,11,11,10,12,11,12,10,10,11,11,
     9,12,11,12,10,11,10,12,10,11,10,12,10,11,10, 7,
     5, 4, 6, 6, 7, 7, 7, 8, 8, 7, 7, 6, 8, 6, 7, 7,
     9, 8, 9, 9,10,11,11,11,12,11,10,11,12,11,12,11,
    12,12,12,12,11,12,12,11,12,11,12,11,13,11,12,10,
    13,10,14,14,13,14,15,14,16,15,15,18,18,18, 9,18,
     8,
};

static const int OffsetCodeLengths_1[11] = {
     5, 6, 3, 3, 3, 3, 3, 3, 3, 4, 6,
};

static const int FirstCodeLengths_2[321] = {
     4, 7, 7, 8, 7, 8, 8, 8, 8, 7, 8, 7, 8, 7, 9, 8,
     8, 8, 9, 9, 9, 9,10,10, 9,10,10,10,10,10, 9, 9,
     5, 9, 8, 9, 9,11,10, 9, 8, 9, 9, 9, 8, 9, 7, 8,
     8, 8, 9, 9, 9, 9, 9,10, 9, 9, 9,10, 9, 9,10, 9,
     8, 8, 7, 7, 7, 8, 8, 9, 8, 8, 9, 9, 8, 8, 7, 8,
     7,10, 8, 7, 7, 9, 9, 9, 9,10,10,11,11,11,10, 9,
     8, 6, 8, 7, 7, 5, 7, 7, 7, 6, 9, 8, 6, 7, 6, 6,
     7, 9, 6, 6, 6, 7, 8, 8, 8, 8, 9,10, 9,10, 9, 9,
     8, 9,10,10, 9,10,10, 9, 9,10,10,10,10,10,10,10,
     9,10,10,11,10,10,10,10,10,10,10,11,10,11,10,10,
     9,11,10,10,10,10,10,10, 9, 9,10,11,10,11,10,11,
    10,12,10,11,10,12,11,12,10,12,10,11,10,11,11,11,
     9,10,11,11,11,12,12,10,10,10,11,11,10,11,10,10,
     9,11,10,11,10,11,11,11,10,11,11,12,11,11,10,10,
    10,11,10,10,11,11,12,10,10,11,11,12,11,11,10,11,
     9,12,10,11,11,11,10,11,10,11,10,11, 9,10, 9, 7,
     3, 5, 6, 6, 7, 7, 8, 8, 8, 9, 9, 9,11,10,10,10,
    12,13,11,12,12,11,13,12,12,11,12,12,13,12,14,13,
    14,13,15,13,14,15,15,14,13,15,15,14,15,14,15,15,
    14,15,13,13,14,15,15,14,14,16,16,15,15,15,12,15,
    10,
};

static const int SecondCodeLengths_2[321] = {
     5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 8, 7, 8, 7, 7,
     7, 8, 8, 8, 8, 9, 8, 9, 8, 9, 9, 9, 7, 9, 8, 8,
     6, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 8,
     8, 8, 8, 9, 8, 9, 8, 9, 9,10, 8,10, 8, 9, 9, 8,
     8, 8, 7, 8, 8, 9, 8, 9, 7, 9, 8,10, 8, 9, 8, 9,
     8, 9, 8, 8, 8, 9, 9, 9, 9,10, 9,11, 9,10, 9,10,
     8, 8, 8, 9, 8, 8, 8, 9, 9, 8, 9,10, 8, 9, 8, 8,
     8,11, 8, 7, 8, 9, 9, 9, 9,10, 9,10, 9,10, 9, 8,
     8, 9, 9,10, 9,10, 9,10, 8,10, 9,10, 9,11,10,11,
     9,11,10,10,10,11, 9,11, 9,10, 9,11, 9,11,10,10,
     9,10, 9, 9, 8,10, 9,11, 9, 9, 9,11,10,11, 9,11,
     9,11, 9,11,10,11,10,11,10,11, 9,10,10,11,10,10,
     8,10, 9,10,10,11, 9,11, 9,10,10,11, 9,10,10, 9,
     9,10, 9,10, 9,10, 9,10, 9,11, 9,11,10,10, 9,10,
     9,11, 9,11, 9,11, 9,10, 9,11, 9,11, 9,11, 9,10,
     8,11, 9,10, 9,10, 9,10, 8,10, 8, 9, 8, 9, 8, 7,
     4, 4, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 7, 8, 8,
     9, 9,10,10,10,10,10,10,11,11,10,10,12,11,11,12,
    12,11,12,12,11,12,12,12,12,12,12,11,12,11,13,12,
    13,12,13,14,14,14,15,13,14,13,14,18,18,17, 7,16,
     9,
};

static const int OffsetCodeLengths_2[13] = {
     5, 6, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 6,
};

static const int FirstCodeLengths_3[321] = {
     6, 6, 6, 6, 6, 9, 8, 8, 4, 9, 8, 9, 8, 9, 9, 9,
     8, 9, 9,10, 8,10,10,10, 9,10,10,10, 9,10,10, 9,
     9, 9, 8,10, 9,10, 9,10, 9,10, 9,10, 9, 9, 8, 9,
     8, 9, 9, 9,10,10,10,10, 9, 9, 9,10, 9,10, 9, 9,
     7, 8, 8, 9, 8, 9, 9, 9, 8, 9, 9,10, 9, 9, 8, 9,
     8, 9, 8, 8, 8, 9, 9, 9, 9, 9,10,10,10,10,10, 9,
     8, 8, 9, 8, 9, 7, 8, 8, 9, 8,10,10, 8, 9, 8, 8,
     8,10, 8, 8, 8, 8, 9, 9, 9, 9,10,10,10,10,10, 9,
     7, 9, 9,10,10,10,10,10, 9,10,10,10,10,10,10, 9,
     9,10,10,10,10,10,10,10,10, 9,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10, 9, 9, 9,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10, 9,10,10,10,10, 9,
     8, 9,10,10,10,10,10,10,10,10,10,10, 9,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10,10,10,10, 9,
     9,10,10,10,10,10,10, 9,10,10,10,10,10,10, 9, 9,
     9,10,10,10,10,10,10, 9, 9,10, 9, 9, 8, 9, 8, 9,
     4, 6, 6, 6, 7, 8, 8, 9, 9,10,10,10, 9,10,10,10,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10, 7,10,
    10,10, 7,10,10, 7, 7, 7, 7, 7, 6, 7,10, 7, 7,10,
     7, 7, 7, 6, 7, 6, 6, 7, 7, 6, 6, 9, 6, 9,10, 6,
    10,
};

static const int SecondCodeLengths_3[321] = {
     5, 6, 6, 6, 6, 7, 7, 7, 6, 8, 7, 8, 7, 9, 8, 8,
     7, 7, 8, 9, 9, 9, 9,10, 8, 9, 9,10, 8,10, 9, 8,
     6,10, 8,10, 8,10, 9, 9, 9, 9, 9,10, 9, 9, 8, 9,
     8, 9, 8, 9, 9,10, 9,10, 9, 9, 8,10, 9,11,10, 8,
     8, 8, 8, 9, 7, 9, 9,10, 8, 9, 8,11, 9,10, 9,10,
     8, 9, 9, 9, 9, 8, 9, 9,10,10,10,12,10,11,10,10,
     8, 9, 9, 9, 8, 9, 8, 8,10, 9,10,11, 8,10, 9, 9,
     8,12, 8, 9, 9, 9, 9, 8, 9,10, 9,12,10,10,10, 8,
     7,11,10, 9,10,11, 9,11, 7,11,10,12,10,12,10,11,
     9,11, 9,12,10,12,10,12,10, 9,11,12,10,12,10,11,
     9,10, 9,10, 9,11,11,12, 9,10, 8,12,11,12, 9,12,
    10,12,10,13,10,12,10,12,10,12,10, 9,10,12,10, 9,
     8,11,10,12,10,12,10,12,10,11,10,12, 8,12,10,11,
    10,10,10,12, 9,11,10,12,10,12,11,12,10, 9,10,12,
     9,10,10,12,10,11,10,11,10,12, 8,12, 9,12, 8,12,
     8,11,10,11,10,11, 9,10, 8,10, 9, 9, 8, 9, 8, 7,
     4, 3, 5, 5, 6, 5, 6, 6, 7, 7, 8, 8, 8, 7, 7, 7,
     9, 8, 9, 9,11, 9,11, 9, 8, 9, 9,11,12,11,12,12,
    13,13,12,13,14,13,14,13,14,13,13,13,12,13,13,12,
    13,13,14,14,13,13,14,14,14,14,15,18,17,18, 8,16,
    10,
};

static const int OffsetCodeLengths_3[14] = {
     6, 7, 4, 4, 3, 3, 3, 3, 3, 4, 4, 4, 5, 7,
};

static const int FirstCodeLengths_4[321] = {
     2, 6, 6, 7, 7, 8, 7, 8, 7, 8, 8, 9, 8, 9, 9, 9,
     8, 8, 9, 9, 9,10,10, 9, 8,10, 9,10, 9,10, 9, 9,
     6, 9, 8, 9, 9,10, 9, 9, 9,10, 9, 9, 9, 9, 8, 8,
     8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,10,10, 9,
     7, 7, 8, 8, 8, 8, 9, 9, 7, 8, 9,10, 8, 8, 7, 8,
     8,10, 8, 8, 8, 9, 8, 9, 9,10, 9,11,10,11, 9, 9,
     8, 7, 9, 8, 8, 6, 8, 8, 8, 7,10, 9, 7, 8, 7, 7,
     8,10, 7, 7, 7, 8, 9, 9, 9, 9,10,11, 9,11,10, 9,
     7, 9,10,10,10,11,11,10,10,11,10,10,10,11,11,10,
     9,10,10,11,10,11,10,11,10,10,10,11,10,11,10,10,
     9,10,10,11,10,10,10,10, 9,10,10,10,10,11,10,11,
    10,11,10,11,11,11,10,12,10,11,10,11,10,11,11,10,
     8,10,10,11,10,11,11,11,10,11,10,11,10,11,11,11,
     9,10,11,11,10,11,11,11,10,11,11,11,10,10,10,10,
    10,11,10,10,11,11,10,10, 9,11,10,10,11,11,10,10,
    10,11,10,10,10,10,10,10, 9,11,10,10, 8,10, 8, 6,
     5, 6, 6, 7, 7, 8, 8, 8, 9,10,11,10,10,11,11,12,
    12,10,11,12,12,12,12,13,13,13,13,13,12,13,13,15,
    14,12,14,15,16,12,12,13,15,14,16,15,17,18,15,17,
    16,15,15,15,15,13,13,10,14,12,13,17,17,18,10,17,
     4,
};

static const int SecondCodeLengths_4[321] = {
     4, 5, 6, 6, 6, 6, 7, 7, 6, 7, 7, 9, 6, 8, 8, 7,
     7, 8, 8, 8, 6, 9, 8, 8, 7, 9, 8, 9, 8, 9, 8, 9,
     6, 9, 8, 9, 8,10, 9, 9, 8,10, 8,10, 8, 9, 8, 9,
     8, 8, 7, 9, 9, 9, 9, 9, 8,10, 9,10, 9,10, 9, 8,
     7, 8, 9, 9, 8, 9, 9, 9, 7,10, 9,10, 9, 9, 8, 9,
     8, 9, 8, 8, 8, 9, 9,10, 9, 9, 8,11, 9,11,10,10,
     8, 8,10, 8, 8, 9, 9, 9,10, 9,10,11, 9, 9, 9, 9,
     8, 9, 8, 8, 8,10,10, 9, 9, 8,10,11,10,11,11, 9,
     8, 9,10,11, 9,10,11,11, 9,12,10,10,10,12,11,11,
     9,11,11,12, 9,11, 9,10,10,10,10,12, 9,11,10,11,
     9,11,11,11,10,11,11,12, 9,10,10,12,11,11,10,11,
     9,11,10,11,10,11, 9,11,11, 9, 8,11,10,11,11,10,
     7,12,11,11,11,11,11,12,10,12,11,13,11,10,12,11,
    10,11,10,11,10,11,11,11,10,12,11,11,10,11,10,10,
    10,11,10,12,11,12,10,11, 9,11,10,11,10,11,10,12,
     9,11,11,11, 9,11,10,10, 9,11,10,10, 9,10, 9, 7,
     4, 5, 5, 5, 6, 6, 7, 6, 8, 7, 8, 9, 9, 7, 8, 8,
    10, 9,10,10,12,10,11,11,11,11,10,11,12,11,11,11,
    11,11,13,12,11,12,13,12,12,12,13,11, 9,12,13, 7,
    13,11,13,11,10,11,13,15,15,12,14,15,15,15, 6,15,
     5,
};

static const int OffsetCodeLengths_4[11] = {
     3, 6, 5, 4, 2, 3, 3, 3, 4, 4, 6,
};

static const int FirstCodeLengths_5[321] = {
     7, 9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 9, 7, 9, 9,
     9, 9, 9, 9, 9, 9, 9,10, 9,10, 9,10, 9,10, 9, 9,
     5, 9, 7, 9, 9, 9, 9, 9, 7, 7, 7, 9, 7, 7, 8, 7,
     8, 8, 7, 7, 9, 9, 9, 9, 7, 7, 7, 9, 9, 9, 9, 9,
     9, 7, 9, 7, 7, 7, 7, 9, 9, 7, 9, 9, 7, 7, 7, 7,
     7, 9, 7, 8, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 7, 8, 7, 7, 7, 8, 8, 6, 7, 9, 7, 7, 8, 7, 5,
     6, 9, 5, 7, 5, 6, 7, 7, 9, 8, 9, 9, 9, 9, 9, 9,
     9, 9,10, 9,10,10,10, 9, 9,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10, 9,10,10,10,
     9,10,10,10, 9, 9,10, 9, 9, 9, 9,10,10,10,10,10,
    10,10,10,10,10,10, 9,10,10,10,10,10,10,10,10,10,
     9,10,10,10, 9,10,10,10, 9, 9, 9,10,10,10,10,10,
     9,10, 9,10,10, 9,10,10, 9,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
     9,10,10,10,10,10,10,10, 9,10, 9,10, 9,10,10, 9,
     5, 6, 8, 8, 7, 7, 7, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
     9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10, 9,10,10, 5,10, 8, 9, 8,
     9,
};

static const int SecondCodeLengths_5[321] = {
     8,10,11,11,11,12,11,11,12, 6,11,12,10, 5,12,12,
    12,12,12,12,12,13,13,14,13,13,12,13,12,13,12,15,
     4,10, 7, 9,11,11,10, 9, 6, 7, 8, 9, 6, 7, 6, 7,
     8, 7, 7, 8, 8, 8, 8, 8, 8, 9, 8, 7,10, 9,10,10,
    11, 7, 8, 6, 7, 8, 8, 9, 8, 7,10,10, 8, 7, 8, 8,
     7,10, 7, 6, 7, 9, 9, 8,11,11,11,10,11,11,11, 8,
    11, 6, 7, 6, 6, 6, 6, 8, 7, 6,10, 9, 6, 7, 6, 6,
     7,10, 6, 5, 6, 7, 7, 7,10, 8,11, 9,13, 7,14,16,
    12,14,14,15,15,16,16,14,15,15,15,15,15,15,15,15,
    14,15,13,14,14,16,15,17,14,17,15,17,12,14,13,16,
    12,17,13,17,14,13,13,14,14,12,13,15,15,14,15,17,
    14,17,15,14,15,16,12,16,15,14,15,16,15,16,17,17,
    15,15,17,17,13,14,15,15,13,12,16,16,17,14,15,16,
    15,15,13,13,15,13,16,17,15,17,17,17,16,17,14,17,
    14,16,15,17,15,15,14,17,15,17,15,16,15,15,16,16,
    14,17,17,15,15,16,15,17,15,14,16,16,16,16,16,12,
     4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9,
     9, 9, 9,10,10,10,11,10,11,11,11,11,11,12,12,12,
    13,13,12,13,12,14,14,12,13,13,13,13,14,12,13,13,
    14,14,14,13,14,14,15,15,13,15,13,17,17,17, 9,17,
     7,
};

static const int OffsetCodeLengths_5[11] = {
     6, 7, 7, 6, 4, 3, 2, 2, 3, 3, 6,
};

static const int *FirstCodeLengths[5] = {
    FirstCodeLengths_1, FirstCodeLengths_2, FirstCodeLengths_3, FirstCodeLengths_4, FirstCodeLengths_5
};

static const int *SecondCodeLengths[5] = {
    SecondCodeLengths_1, SecondCodeLengths_2, SecondCodeLengths_3, SecondCodeLengths_4, SecondCodeLengths_5
};

static const int *OffsetCodeLengths[5] = {
    OffsetCodeLengths_1, OffsetCodeLengths_2, OffsetCodeLengths_3, OffsetCodeLengths_4, OffsetCodeLengths_5
};

static const int OffsetCodeSize[5] = { 11, 13, 14, 11, 11 };

/* Fixed "meta" prefix code used to decode a dynamic code's own
 * length-run description (case 0 of the control byte). Codes are
 * given low-bit-first here, matching the reference's
 * addValue:forCodeWithLowBitFirst: - reversed via sit_pc_reverse_bits
 * before insertion. */
static const int MetaCodes[37] = {
    0x5d8,0x058,0x040,0x0c0,0x000,0x078,0x02b,0x014,
    0x00c,0x01c,0x01b,0x00b,0x010,0x020,0x038,0x018,
    0x0d8,0xbd8,0x180,0x680,0x380,0xf80,0x780,0x480,
    0x080,0x280,0x3d8,0xfd8,0x7d8,0x9d8,0x1d8,0x004,
    0x001,0x002,0x007,0x003,0x008
};

static const int MetaCodeLengths[37] = {
    11,8,8,8,8,7,6,5,5,5,5,6,5,6,7,7,9,12,10,11,11,12,
    12,11,11,11,12,12,12,12,12,5,2,2,3,4,5
};

/* ------------------------------------------------------------------ */
/* Dynamic code-length-run parser (control byte code==0 case), ported  */
/* from -allocAndParseCodeOfSize:metaCode:.                            */
/* ------------------------------------------------------------------ */
static int lzh13_build_dynamic_code(sit_prefix_code *pc, int numsymbols,
                                     sit_prefix_code *metacode, lzh13_br *br,
                                     int *lengths /* scratch, >= numsymbols ints */)
{
    int length = 0;

    for (int i = 0; i < numsymbols; i++) {
        int val = sit_pc_decode_bits(metacode, lzh13_bitfn, br);
        if (val < 0) return val;

        switch (val) {
        case 31:
            length = -1;
            break;
        case 32:
            length++;
            break;
        case 33:
            length--;
            break;
        case 34: {
            int b = lzh13_getbit(br);
            if (b < 0) return SIT_E_IO;
            if (b) {
                if (i >= numsymbols) return SIT_E_CORRUPT;
                lengths[i++] = length;
            }
            break;
        }
        case 35: {
            int cnt = lzh13_getbits(br, 3);
            if (cnt < 0) return SIT_E_IO;
            cnt += 2;
            while (cnt--) {
                if (i >= numsymbols) return SIT_E_CORRUPT;
                lengths[i++] = length;
            }
            break;
        }
        case 36: {
            int cnt = lzh13_getbits(br, 6);
            if (cnt < 0) return SIT_E_IO;
            cnt += 10;
            while (cnt--) {
                if (i >= numsymbols) return SIT_E_CORRUPT;
                lengths[i++] = length;
            }
            break;
        }
        default:
            length = val + 1;
            break;
        }

        if (i >= numsymbols) return SIT_E_CORRUPT;
        lengths[i] = length;
    }

    return sit_pc_build_from_lengths(pc, lengths, numsymbols, 32);
}

/* ------------------------------------------------------------------ */
#define LZH13_WINDOW_SIZE 65536u
#define LZH13_WINDOW_MASK (LZH13_WINDOW_SIZE - 1u)
#define LZH13_END_SYMBOL   0x140
#define LZH13_OUTBUF       2048

int sit_lzh13_decompress(sit_io *io, uint32_t outlen,
                          sit_sink_fn sink, void *ctx,
                          const sit_allocator *alloc)
{
    /* Leading control byte - a single plain byte read (CSInputNextByte),
     * not bit-buffered, so it must happen before the LE bit reader
     * below ever touches the stream. */
    int ctrlbyte = sit_io_getbyte(io);
    if (ctrlbyte < 0) return SIT_E_IO;
    int codesel = (ctrlbyte >> 4) & 0x0F;

    lzh13_br br;
    lzh13_br_init(&br, io);

    sit_prefix_code firstcode = { 0, 0, 0 };
    sit_prefix_code secondcode = { 0, 0, 0 };
    sit_prefix_code offsetcode = { 0, 0, 0 };
    int shared_second = 0;
    int rc;

    if (codesel == 0) {
        sit_prefix_code metacode = { 0, 0, 0 };
        rc = sit_pc_init(&metacode, 2 * 37 + 4, alloc);
        if (rc != SIT_OK) return rc;

        for (int i = 0; i < 37; i++) {
            uint32_t code = sit_pc_reverse_bits((uint32_t)MetaCodes[i], MetaCodeLengths[i]);
            rc = sit_pc_add_symbol(&metacode, i, code, MetaCodeLengths[i]);
            if (rc != SIT_OK) { sit_pc_free(&metacode, alloc); return rc; }
        }

        int *lengths = alloc->alloc(sizeof(int) * (size_t)SIT_LZH13_NUMCODES, alloc->ctx);
        if (!lengths) { sit_pc_free(&metacode, alloc); return SIT_E_NOMEM; }

        rc = sit_pc_init(&firstcode, 2 * SIT_LZH13_NUMCODES + 4, alloc);
        if (rc == SIT_OK) rc = lzh13_build_dynamic_code(&firstcode, SIT_LZH13_NUMCODES, &metacode, &br, lengths);
        if (rc != SIT_OK) {
            alloc->free(lengths, alloc->ctx);
            sit_pc_free(&metacode, alloc);
            sit_pc_free(&firstcode, alloc);
            return rc;
        }

        if (ctrlbyte & 0x08) {
            secondcode = firstcode;
            shared_second = 1;
        } else {
            rc = sit_pc_init(&secondcode, 2 * SIT_LZH13_NUMCODES + 4, alloc);
            if (rc == SIT_OK) rc = lzh13_build_dynamic_code(&secondcode, SIT_LZH13_NUMCODES, &metacode, &br, lengths);
            if (rc != SIT_OK) {
                alloc->free(lengths, alloc->ctx);
                sit_pc_free(&metacode, alloc);
                sit_pc_free(&firstcode, alloc);
                sit_pc_free(&secondcode, alloc);
                return rc;
            }
        }

        int offsetsize = (ctrlbyte & 0x07) + 10;
        rc = sit_pc_init(&offsetcode, 2 * offsetsize + 4, alloc);
        if (rc == SIT_OK) rc = lzh13_build_dynamic_code(&offsetcode, offsetsize, &metacode, &br, lengths);

        alloc->free(lengths, alloc->ctx);
        sit_pc_free(&metacode, alloc);

        if (rc != SIT_OK) {
            sit_pc_free(&firstcode, alloc);
            if (!shared_second) sit_pc_free(&secondcode, alloc);
            sit_pc_free(&offsetcode, alloc);
            return rc;
        }
    } else if (codesel >= 1 && codesel <= 5) {
        int idx = codesel - 1;

        rc = sit_pc_init(&firstcode, 2 * SIT_LZH13_NUMCODES + 4, alloc);
        if (rc == SIT_OK) rc = sit_pc_build_from_lengths(&firstcode, FirstCodeLengths[idx], SIT_LZH13_NUMCODES, 32);
        if (rc != SIT_OK) { sit_pc_free(&firstcode, alloc); return rc; }

        rc = sit_pc_init(&secondcode, 2 * SIT_LZH13_NUMCODES + 4, alloc);
        if (rc == SIT_OK) rc = sit_pc_build_from_lengths(&secondcode, SecondCodeLengths[idx], SIT_LZH13_NUMCODES, 32);
        if (rc != SIT_OK) { sit_pc_free(&firstcode, alloc); sit_pc_free(&secondcode, alloc); return rc; }

        int offsize = OffsetCodeSize[idx];
        rc = sit_pc_init(&offsetcode, 2 * offsize + 4, alloc);
        if (rc == SIT_OK) rc = sit_pc_build_from_lengths(&offsetcode, OffsetCodeLengths[idx], offsize, 32);
        if (rc != SIT_OK) {
            sit_pc_free(&firstcode, alloc);
            sit_pc_free(&secondcode, alloc);
            sit_pc_free(&offsetcode, alloc);
            return rc;
        }
    } else {
        return SIT_E_CORRUPT; /* control nibble 6-15: invalid (reference raises an exception) */
    }

    /* ---- main LZ77 decode, ported from XADLZSSHandle produceByteAtOffset: ---- */
    uint8_t *window = alloc->alloc((size_t)LZH13_WINDOW_SIZE, alloc->ctx);
    if (!window) {
        sit_pc_free(&firstcode, alloc);
        if (!shared_second) sit_pc_free(&secondcode, alloc);
        sit_pc_free(&offsetcode, alloc);
        return SIT_E_NOMEM;
    }
    memset(window, 0, LZH13_WINDOW_SIZE);

    /* outbuf (2KB) is heap-allocated alongside window rather than kept
     * as a stack local, for the same reason. */
    uint8_t *outbuf = alloc->alloc(LZH13_OUTBUF, alloc->ctx);
    if (!outbuf) {
        alloc->free(window, alloc->ctx);
        sit_pc_free(&firstcode, alloc);
        if (!shared_second) sit_pc_free(&secondcode, alloc);
        sit_pc_free(&offsetcode, alloc);
        return SIT_E_NOMEM;
    }

    sit_prefix_code *currcode = &firstcode;
    uint32_t pos = 0;
    uint32_t matchoffset = 0;
    int matchlength = 0;

    size_t outpos = 0;

    rc = SIT_OK;

    while (pos < outlen) {
        if (matchlength == 0) {
            int val = sit_pc_decode_bits(currcode, lzh13_bitfn, &br);
            if (val < 0) { rc = val; break; }

            if (val < 0x100) {
                uint8_t b = (uint8_t)val;
                window[pos & LZH13_WINDOW_MASK] = b;
                outbuf[outpos++] = b;
                pos++;
                currcode = &firstcode;

                if (outpos == LZH13_OUTBUF) {
                    if (sink(outbuf, outpos, ctx)) { rc = SIT_E_IO; break; }
                    outpos = 0;
                }
                continue;
            } else if (val == LZH13_END_SYMBOL) {
                /* Premature: we still need more bytes than the stream
                 * claims to have (outlen not yet reached). */
                rc = SIT_E_CORRUPT;
                break;
            } else {
                currcode = &secondcode;

                int length;
                if (val < 0x13e) {
                    length = val - 0x100 + 3;
                } else if (val == 0x13e) {
                    int extra = lzh13_getbits(&br, 10);
                    if (extra < 0) { rc = SIT_E_IO; break; }
                    length = extra + 65;
                } else { /* val == 0x13f */
                    int extra = lzh13_getbits(&br, 15);
                    if (extra < 0) { rc = SIT_E_IO; break; }
                    length = extra + 65;
                }

                int bitlength = sit_pc_decode_bits(&offsetcode, lzh13_bitfn, &br);
                if (bitlength < 0) { rc = bitlength; break; }

                int offset;
                if (bitlength == 0) {
                    offset = 1;
                } else if (bitlength == 1) {
                    offset = 2;
                } else {
                    int extra = lzh13_getbits(&br, bitlength - 1);
                    if (extra < 0) { rc = SIT_E_IO; break; }
                    offset = (1 << (bitlength - 1)) + extra + 1;
                }

                matchlength = length;
                matchoffset = pos - (uint32_t)offset;
            }
        }

        matchlength--;
        uint8_t b = window[matchoffset & LZH13_WINDOW_MASK];
        matchoffset++;
        window[pos & LZH13_WINDOW_MASK] = b;
        outbuf[outpos++] = b;
        pos++;

        if (outpos == LZH13_OUTBUF) {
            if (sink(outbuf, outpos, ctx)) { rc = SIT_E_IO; break; }
            outpos = 0;
        }
    }

    if (rc == SIT_OK && outpos) {
        if (sink(outbuf, outpos, ctx)) rc = SIT_E_IO;
    }

    alloc->free(outbuf, alloc->ctx);
    alloc->free(window, alloc->ctx);
    sit_pc_free(&firstcode, alloc);
    if (!shared_second) sit_pc_free(&secondcode, alloc);
    sit_pc_free(&offsetcode, alloc);

    return rc;
}
