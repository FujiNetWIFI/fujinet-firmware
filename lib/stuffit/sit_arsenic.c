/*
 * sit_arsenic.c - Arsenic (StuffIt method 15) decompressor.
 * Ported from XADStuffItArsenicHandle.m/.h, BWT.c/BWT.h and the bit-level
 * primitives of CSInputBuffer.h/.m, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * Stream format (as implemented by XADStuffItArsenicHandle resetByteStream/
 * readBlock/produceByteAtOffset, verified against real StuffIt 5 archives):
 *
 *   header:  'A' 's' (each an 8-bit raw value decoded through the shared
 *            "initialmodel", a 0/1 bit model reused for every raw
 *            bit/byte field in the stream - see arsenic_decode_bitstring),
 *            then a 4-bit block-size exponent N -> blockbits = N+9,
 *            blocksize = 1<<blockbits (2^9 .. 2^24 bytes), then one bit
 *            through initialmodel that is YES only for a completely empty
 *            (zero-block) stream.
 *
 *   block:   1 bit "randomized" flag, blockbits-wide "primary index" for
 *            the inverse BWT, then a sequence of symbols out of an
 *            11-way "selector" model (0..10):
 *              0,1   - zero-run-length bits (bzip2-style RUNA/RUNB
 *                      unary-in-binary coding: consecutive 0/1 selector
 *                      draws accumulate zerocount += zerostate or
 *                      2*zerostate, zerostate doubling each draw) that
 *                      expand to that many repeats of MTF symbol 0;
 *              2     - literal MTF symbol 1 (no further model draw);
 *              3..9  - MTF symbol decoded from mtfmodel[selector-3], one
 *                      of 7 models partitioning symbols 2..255 by bit
 *                      width (2-3,4-7,8-15,16-31,32-63,64-127,128-255);
 *              10    - end of block's symbol run.
 *            Each decoded MTF symbol is expanded through the shared MTF
 *            table (sit_bwt.h) to a real byte and appended to the
 *            block[] buffer (this is post-BWT, pre-inverse-transform
 *            data). After selector 10, the selector/mtf models reset to
 *            their initial frequencies (order-0 per block) and one more
 *            initialmodel bit says whether a 32-bit trailing CRC
 *            (unused/unverified here - see stuffit.h) follows and this
 *            is the last block.
 *
 *   bytes:   sit_bwt_inverse_transform() turns block[]/primary-index into
 *            the pre-BWT byte sequence; StuffIt's "randomization" XORs
 *            bit 0 of specific byte positions (schedule given by the
 *            fixed RandomizationTable, only when the block's randomized
 *            flag is set - not observed in the two sample archives, but
 *            ported faithfully); finally a byte-oriented RLE4 pass
 *            expands runs (4 equal bytes followed by an extra length
 *            byte, 0 meaning "no run, keep going") back to the original
 *            data. This last RLE run can straddle a block boundary (the
 *            reference's "repeat" counter is not reset when a new block
 *            is read - ported as-is).
 *
 * The arithmetic/range coder itself (ArithmeticModel/ArithmeticDecoder,
 * NumBits=26 fixed-point range coder) is XADStuffItArsenicHandle.m's own
 * inline implementation, not CarrylessRangeCoder.h/.m (that class is
 * unused by this handle) - ported verbatim below.
 */
#include "sit_arsenic.h"
#include "sit_bwt.h"
#include <string.h>

/* ------------------------------------------------------------------- */
/* Raw MSB-first bit source over sit_io.                                */
/*                                                                       */
/* Only two raw (non-modeled) bit reads occur in the whole format: the  */
/* 26-bit initial code fill, and single bits during range-coder         */
/* renormalization (CSInputNextBit in the reference). Both are served   */
/* by bitreader_getbit()/bitreader_getbits() below, one bit at a time - */
/* simpler than porting CSInputBuffer's 32-bit look-ahead accumulator   */
/* and behaviourally identical (same MSB-first byte order).             */
/*                                                                       */
/* A well-formed range-coded stream can legitimately need a handful of  */
/* extra bits past the declared compressed length (renormalization      */
/* look-ahead for the very last symbols) - sit_io simply ends its       */
/* window there, so those reads are satisfied with synthetic zero bits  */
/* instead of failing outright. A large run of such padding almost      */
/* certainly means the stream is corrupt/truncated, so it is capped.    */
/* ------------------------------------------------------------------- */
#define ARSENIC_MAX_PAD_BYTES 16

typedef struct {
    sit_io *io;
    unsigned buf;
    int nbits;
    int pad_bytes;
    int hard_error;
} arsenic_br;

static void br_init(arsenic_br *br, sit_io *io)
{
    br->io = io;
    br->buf = 0;
    br->nbits = 0;
    br->pad_bytes = 0;
    br->hard_error = 0;
}

static unsigned br_getbit(arsenic_br *br)
{
    if (br->nbits == 0) {
        int c = sit_io_getbyte(br->io);
        if (c < 0) {
            br->pad_bytes++;
            if (br->pad_bytes > ARSENIC_MAX_PAD_BYTES) br->hard_error = 1;
            c = 0;
        } else {
            br->pad_bytes = 0;
        }
        br->buf = (unsigned)c;
        br->nbits = 8;
    }
    br->nbits--;
    return (br->buf >> br->nbits) & 1u;
}

static uint32_t br_getbits(arsenic_br *br, int n)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < n; i++) v = (v << 1) | br_getbit(br);
    return v;
}

/* ------------------------------------------------------------------- */
/* Adaptive frequency model (ArithmeticModel).                          */
/* ------------------------------------------------------------------- */
typedef struct {
    int symbol;
    int frequency;
} arsenic_symfreq;

typedef struct {
    int totalfrequency;
    int increment;
    int frequencylimit;
    int numsymbols;
    arsenic_symfreq symbols[128];
} arsenic_model;

static void model_reset(arsenic_model *m)
{
    int i;
    m->totalfrequency = m->increment * m->numsymbols;
    for (i = 0; i < m->numsymbols; i++) m->symbols[i].frequency = m->increment;
}

static void model_init(arsenic_model *m, int firstsymbol, int lastsymbol,
                        int increment, int frequencylimit)
{
    int i;
    m->increment = increment;
    m->frequencylimit = frequencylimit;
    m->numsymbols = lastsymbol - firstsymbol + 1;
    for (i = 0; i < m->numsymbols; i++) m->symbols[i].symbol = i + firstsymbol;
    model_reset(m);
}

static void model_bump(arsenic_model *m, int symindex)
{
    m->symbols[symindex].frequency += m->increment;
    m->totalfrequency += m->increment;

    if (m->totalfrequency > m->frequencylimit) {
        int i;
        m->totalfrequency = 0;
        for (i = 0; i < m->numsymbols; i++) {
            m->symbols[i].frequency++;
            m->symbols[i].frequency >>= 1;
            m->totalfrequency += m->symbols[i].frequency;
        }
    }
}

/* ------------------------------------------------------------------- */
/* Range decoder (ArithmeticDecoder). Verbatim port of the reference's  */
/* own inline range coder (NumBits=26 fixed point) - not                */
/* CarrylessRangeCoder.h/.m, which XADStuffItArsenicHandle.m does not   */
/* use.                                                                  */
/* ------------------------------------------------------------------- */
#define ARSENIC_NUMBITS 26
#define ARSENIC_ONE  (UINT32_C(1) << (ARSENIC_NUMBITS - 1))
#define ARSENIC_HALF (UINT32_C(1) << (ARSENIC_NUMBITS - 2))

typedef struct {
    arsenic_br *br;
    uint32_t range;
    uint32_t code;
} arsenic_decoder;

static void decoder_init(arsenic_decoder *dec, arsenic_br *br)
{
    dec->br = br;
    dec->range = ARSENIC_ONE;
    dec->code = br_getbits(br, ARSENIC_NUMBITS);
}

static void decoder_update(arsenic_decoder *dec, int symlow, int symsize, int symtot)
{
    uint32_t renorm = dec->range / (uint32_t)symtot;
    uint32_t lowincr = renorm * (uint32_t)symlow;

    dec->code -= lowincr;
    if (symlow + symsize == symtot) dec->range -= lowincr;
    else dec->range = renorm * (uint32_t)symsize;

    while (dec->range <= ARSENIC_HALF) {
        dec->range <<= 1;
        dec->code = (dec->code << 1) | br_getbit(dec->br);
    }
}

static int decoder_symbol(arsenic_decoder *dec, arsenic_model *m)
{
    uint32_t frequency = dec->code / (dec->range / (uint32_t)m->totalfrequency);
    int cumulative = 0, n;

    for (n = 0; n < m->numsymbols - 1; n++) {
        if ((uint32_t)(cumulative + m->symbols[n].frequency) > frequency) break;
        cumulative += m->symbols[n].frequency;
    }

    decoder_update(dec, cumulative, m->symbols[n].frequency, m->totalfrequency);
    model_bump(m, n);

    return m->symbols[n].symbol;
}

/* Decodes `bits` raw bits, one arithmetic-coded binary symbol at a time
 * through model m (always the 0/1 "initialmodel" in this format) -
 * NextArithmeticBitString in the reference. Bit i of the result comes
 * from the i-th symbol draw (LSB first). */
static uint32_t decoder_bitstring(arsenic_decoder *dec, arsenic_model *m, int bits)
{
    uint32_t res = 0;
    int i;
    for (i = 0; i < bits; i++) {
        if (decoder_symbol(dec, m)) res |= (uint32_t)1 << i;
    }
    return res;
}

/* ------------------------------------------------------------------- */
/* Byte de-randomization schedule (RandomizationTable, verbatim).       */
/* ------------------------------------------------------------------- */
static const uint16_t ArsenicRandomizationTable[256] = {
    0xee,  0x56,  0xf8,  0xc3,  0x9d,  0x9f,  0xae,  0x2c,
    0xad,  0xcd,  0x24,  0x9d,  0xa6, 0x101,  0x18,  0xb9,
    0xa1,  0x82,  0x75,  0xe9,  0x9f,  0x55,  0x66,  0x6a,
    0x86,  0x71,  0xdc,  0x84,  0x56,  0x96,  0x56,  0xa1,
    0x84,  0x78,  0xb7,  0x32,  0x6a,   0x3,  0xe3,   0x2,
    0x11, 0x101,   0x8,  0x44,  0x83, 0x100,  0x43,  0xe3,
    0x1c,  0xf0,  0x86,  0x6a,  0x6b,   0xf,   0x3,  0x2d,
    0x86,  0x17,  0x7b,  0x10,  0xf6,  0x80,  0x78,  0x7a,
    0xa1,  0xe1,  0xef,  0x8c,  0xf6,  0x87,  0x4b,  0xa7,
    0xe2,  0x77,  0xfa,  0xb8,  0x81,  0xee,  0x77,  0xc0,
    0x9d,  0x29,  0x20,  0x27,  0x71,  0x12,  0xe0,  0x6b,
    0xd1,  0x7c,   0xa,  0x89,  0x7d,  0x87,  0xc4, 0x101,
    0xc1,  0x31,  0xaf,  0x38,   0x3,  0x68,  0x1b,  0x76,
    0x79,  0x3f,  0xdb,  0xc7,  0x1b,  0x36,  0x7b,  0xe2,
    0x63,  0x81,  0xee,   0xc,  0x63,  0x8b,  0x78,  0x38,
    0x97,  0x9b,  0xd7,  0x8f,  0xdd,  0xf2,  0xa3,  0x77,
    0x8c,  0xc3,  0x39,  0x20,  0xb3,  0x12,  0x11,   0xe,
    0x17,  0x42,  0x80,  0x2c,  0xc4,  0x92,  0x59,  0xc8,
    0xdb,  0x40,  0x76,  0x64,  0xb4,  0x55,  0x1a,  0x9e,
    0xfe,  0x5f,   0x6,  0x3c,  0x41,  0xef,  0xd4,  0xaa,
    0x98,  0x29,  0xcd,  0x1f,   0x2,  0xa8,  0x87,  0xd2,
    0xa0,  0x93,  0x98,  0xef,   0xc,  0x43,  0xed,  0x9d,
    0xc2,  0xeb,  0x81,  0xe9,  0x64,  0x23,  0x68,  0x1e,
    0x25,  0x57,  0xde,  0x9a,  0xcf,  0x7f,  0xe5,  0xba,
    0x41,  0xea,  0xea,  0x36,  0x1a,  0x28,  0x79,  0x20,
    0x5e,  0x18,  0x4e,  0x7c,  0x8e,  0x58,  0x7a,  0xef,
    0x91,   0x2,  0x93,  0xbb,  0x56,  0xa1,  0x49,  0x1b,
    0x79,  0x92,  0xf3,  0x58,  0x4f,  0x52,  0x9c,   0x2,
    0x77,  0xaf,  0x2a,  0x8f,  0x49,  0xd0,  0x99,  0x4d,
    0x98, 0x101,  0x60,  0x93, 0x100,  0x75,  0x31,  0xce,
    0x49,  0x20,  0x56,  0x57,  0xe2,  0xf5,  0x26,  0x2b,
    0x8a,  0xbf,  0xde,  0xd0,  0x83,  0x34,  0xf4,  0x17
};

/* ------------------------------------------------------------------- */
/* Top-level Arsenic state.                                             */
/* ------------------------------------------------------------------- */
typedef struct {
    arsenic_br br;
    arsenic_decoder dec;

    arsenic_model initialmodel;
    arsenic_model selectormodel;
    arsenic_model mtfmodel[7];

    sit_mtf_state mtf;

    int blockbits;
    uint32_t blocksize;
    uint8_t *block;         /* [blocksize], post-MTF-decode block bytes */
    uint8_t *transform;     /* [3*blocksize], packed 24-bit LE inverse-BWT
                              * successor vector - see sit_bwt_transform_get/
                              * set in sit_bwt.h */

    int endofblocks;

    uint32_t numbytes;      /* valid bytes in block[]/transform[] */
    uint32_t bytecount;     /* bytes consumed from the current block */
    uint32_t transformindex;

    int randomized;
    uint32_t randcount;
    int randindex;

    /* final RLE4 pass state - deliberately NOT reset on block change,
     * matching the reference (a run can straddle two blocks). */
    int repeat;
    int count;
    int last;
} arsenic_state;

static int arsenic_open(arsenic_state *st, sit_io *io, sit_progress *prog)
{
    br_init(&st->br, io);
    decoder_init(&st->dec, &st->br);

    model_init(&st->initialmodel, 0, 1, 1, 256);
    model_init(&st->selectormodel, 0, 10, 8, 1024);
    model_init(&st->mtfmodel[0], 2, 3, 8, 1024);
    model_init(&st->mtfmodel[1], 4, 7, 4, 1024);
    model_init(&st->mtfmodel[2], 8, 15, 4, 1024);
    model_init(&st->mtfmodel[3], 16, 31, 4, 1024);
    model_init(&st->mtfmodel[4], 32, 63, 2, 1024);
    model_init(&st->mtfmodel[5], 64, 127, 2, 1024);
    model_init(&st->mtfmodel[6], 128, 255, 1, 1024);

    if (decoder_bitstring(&st->dec, &st->initialmodel, 8) != 'A') return SIT_E_CORRUPT;
    if (decoder_bitstring(&st->dec, &st->initialmodel, 8) != 's') return SIT_E_CORRUPT;

    st->blockbits = (int)decoder_bitstring(&st->dec, &st->initialmodel, 4) + 9;
    st->blocksize = UINT32_C(1) << st->blockbits;

    if (prog) prog->block_size = st->blocksize;

    st->numbytes = 0;
    st->bytecount = 0;
    st->repeat = 0;
    st->count = 0;
    st->last = 0;
    st->block = NULL;
    st->transform = NULL;

    st->endofblocks = decoder_symbol(&st->dec, &st->initialmodel);

    if (st->br.hard_error) return SIT_E_IO;
    return SIT_OK;
}

static int arsenic_read_block(arsenic_state *st)
{
    sit_mtf_reset(&st->mtf);

    st->randomized = decoder_symbol(&st->dec, &st->initialmodel);
    st->transformindex = decoder_bitstring(&st->dec, &st->initialmodel, st->blockbits);
    st->numbytes = 0;

    for (;;) {
        int sel = decoder_symbol(&st->dec, &st->selectormodel);

        if (sel == 0 || sel == 1) {
            uint32_t zerostate = 1, zerocount = 0;
            while (sel < 2) {
                if (sel == 0) zerocount += zerostate;
                else zerocount += 2 * zerostate;
                zerostate *= 2;
                sel = decoder_symbol(&st->dec, &st->selectormodel);
            }

            if (st->numbytes + zerocount > st->blocksize) return SIT_E_CORRUPT;
            memset(&st->block[st->numbytes], sit_mtf_decode(&st->mtf, 0), zerocount);
            st->numbytes += zerocount;
        }

        if (sel == 10) break;

        if (st->br.hard_error) return SIT_E_IO;

        {
            int symbol;
            if (sel == 2) symbol = 1;
            else if (sel >= 3 && sel <= 9) symbol = decoder_symbol(&st->dec, &st->mtfmodel[sel - 3]);
            else return SIT_E_CORRUPT;

            if (st->numbytes >= st->blocksize) return SIT_E_CORRUPT;
            st->block[st->numbytes++] = sit_mtf_decode(&st->mtf, (uint8_t)symbol);
        }
    }

    if (st->numbytes == 0 || st->transformindex >= st->numbytes) return SIT_E_CORRUPT;

    model_reset(&st->selectormodel);
    {
        int i;
        for (i = 0; i < 7; i++) model_reset(&st->mtfmodel[i]);
    }

    if (decoder_symbol(&st->dec, &st->initialmodel)) {
        (void)decoder_bitstring(&st->dec, &st->initialmodel, 32); /* trailing CRC - unused, see stuffit.h */
        st->endofblocks = 1;
    }

    if (st->br.hard_error) return SIT_E_IO;

    sit_bwt_inverse_transform(st->transform, st->block, st->numbytes);
    return SIT_OK;
}

/* Produces exactly one final-output byte per call (looping internally
 * past zero-length RLE4 markers, which carry no output of their own) -
 * a streaming equivalent of produceByteAtOffset:. Returns SIT_OK with
 * *outbyte filled in, SIT_E_EOF once every block has been fully
 * consumed and the stream's end marker was seen, or another SIT_E_*
 * code on a malformed stream. */
static int arsenic_next_byte(arsenic_state *st, uint8_t *outbyte)
{
    int byte;

    if (st->repeat) {
        st->repeat--;
        byte = st->last;
    } else {
        for (;;) {
            if (st->bytecount >= st->numbytes) {
                if (st->endofblocks) return SIT_E_EOF;

                int rc = arsenic_read_block(st);
                if (rc != SIT_OK) return rc;

                st->bytecount = 0;
                st->count = 0;
                st->last = 0;

                st->randindex = 0;
                st->randcount = ArsenicRandomizationTable[0];
            }

            st->transformindex = sit_bwt_transform_get(st->transform, st->transformindex);
            byte = st->block[st->transformindex];

            if (st->randomized && st->randcount == st->bytecount) {
                byte ^= 1;
                st->randindex = (st->randindex + 1) & 255;
                st->randcount += ArsenicRandomizationTable[st->randindex];
            }

            st->bytecount++;

            if (st->count == 4) {
                st->count = 0;
                if (byte == 0) continue; /* no run here after all - keep going */
                st->repeat = byte - 1;
                byte = st->last;
            } else {
                if (byte == st->last) st->count++;
                else { st->count = 1; st->last = byte; }
            }
            break;
        }
    }

    if (st->br.hard_error) return SIT_E_IO;

    *outbyte = (uint8_t)byte;
    return SIT_OK;
}

/* ------------------------------------------------------------------- */
/* Public entry point.                                                   */
/* ------------------------------------------------------------------- */
#define ARSENIC_OUTBUF 4096

int sit_arsenic_decompress(sit_io *io, uint32_t outlen,
                            sit_sink_fn sink, void *ctx,
                            const sit_allocator *alloc,
                            sit_progress *prog)
{
    /* arsenic_state is ~9.7KB (7 mtfmodel[] entries plus initialmodel/
     * selectormodel, each holding arsenic_symfreq symbols[128]) -
     * heap-allocated here instead of kept as a local so it doesn't sit
     * on a constrained caller's stack for the whole decode. */
    arsenic_state *st;
    uint8_t *outbuf;
    int rc;

    st = (arsenic_state *)alloc->alloc(sizeof(*st), alloc->ctx);
    if (!st) return SIT_E_NOMEM;

    rc = arsenic_open(st, io, prog);
    if (rc != SIT_OK) { alloc->free(st, alloc->ctx); return rc; }

    if (outlen == 0) { alloc->free(st, alloc->ctx); return SIT_OK; }

    st->block = alloc->alloc((size_t)st->blocksize, alloc->ctx);
    if (!st->block) { alloc->free(st, alloc->ctx); return SIT_E_NOMEM; }

    st->transform = alloc->alloc((size_t)3 * (size_t)st->blocksize, alloc->ctx);
    if (!st->transform) {
        alloc->free(st->block, alloc->ctx);
        alloc->free(st, alloc->ctx);
        return SIT_E_NOMEM;
    }

    outbuf = (uint8_t *)alloc->alloc(ARSENIC_OUTBUF, alloc->ctx);
    if (!outbuf) {
        alloc->free(st->transform, alloc->ctx);
        alloc->free(st->block, alloc->ctx);
        alloc->free(st, alloc->ctx);
        return SIT_E_NOMEM;
    }

    {
        size_t outfill = 0;
        uint32_t produced = 0;

        while (produced < outlen) {
            uint8_t b;

            rc = arsenic_next_byte(st, &b);
            if (rc != SIT_OK) {
                if (rc == SIT_E_EOF) rc = SIT_E_IO; /* fewer bytes than outlen promised */
                break;
            }

            outbuf[outfill++] = b;
            produced++;
            if (prog) prog->bytes_out = produced;

            if (outfill == ARSENIC_OUTBUF || produced == outlen) {
                if (sink(outbuf, outfill, ctx) != 0) { rc = SIT_E_IO; break; }
                outfill = 0;
            }
        }

        if (produced == outlen) rc = SIT_OK;
    }

    alloc->free(outbuf, alloc->ctx);
    alloc->free(st->transform, alloc->ctx);
    alloc->free(st->block, alloc->ctx);
    alloc->free(st, alloc->ctx);

    return rc;
}
