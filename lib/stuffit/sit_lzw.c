/*
 * sit_lzw.c - classic Unix "compress"-style adaptive LZW decompressor
 * (StuffIt compression method 2). Ported from LZW.c/LZW.h and
 * XADCompressHandle.m/.h, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * StuffIt always instantiates XADCompressHandle with flags=0x8e:
 * maxsymbols = 1<<(flags&0x1f) = 1<<14 = 16384, blockmode =
 * (flags&0x80)!=0 = true (reservedsymbols = blockmode?1:0 = 1,
 * reserving code 256 as a "clear the dictionary" signal). Those
 * values are therefore hardcoded here rather than threaded through as
 * parameters.
 *
 * IMPORTANT bit order: XADCompressHandle.m reads codes via
 * CSInputNextBitStringLE - LSB-first ("little-endian") bit packing,
 * the same convention as classic Unix compress(1) - which is
 * DIFFERENT from the shared MSB-first sit_bitreader.h used by methods
 * 3 and 13. This file therefore implements its own small self-
 * contained LSB-first bit reader below instead of reusing
 * sit_bitreader.h.
 *
 * No sample in this project's test corpus exercises method 2 (both
 * StuffIt5 samples use methods 15/0, the one classic-format sample
 * uses methods 13/0) - this port is verified only by a synthetic
 * self-test under AddressSanitizer, NOT against real StuffIt data.
 */
#include <string.h>
#include "sit_internal.h"

/* ------------------------------------------------------------------ */
/* LSB-first ("LE") bit reader over sit_io, local to this file.        */
/* ------------------------------------------------------------------ */
typedef struct {
    sit_io *io;
    uint32_t bitbuf;  /* bits accumulate at the low end; next bit is bit 0 */
    int nbits;
    int error;
} lzw_br;

static void lzw_br_init(lzw_br *br, sit_io *io)
{
    br->io = io;
    br->bitbuf = 0;
    br->nbits = 0;
    br->error = 0;
}

/* n must be <= 24: a fetch only ever runs while nbits<n<=24, so nbits
 * never needs to exceed 31 before extraction - always safe in a
 * 32-bit accumulator. */
static int lzw_br_getbits(lzw_br *br, int n)
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

/* Skip n bits; n can be up to symbolsize*7 (<=14*7=98), chunked
 * through lzw_br_getbits in pieces of <=24 bits each. */
static int lzw_br_skipbits(lzw_br *br, int n)
{
    while (n > 0) {
        int take = n > 24 ? 24 : n;
        if (lzw_br_getbits(br, take) < 0) return -1;
        n -= take;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* LZW dictionary, ported from LZW.c/LZW.h.                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t chr;
    int32_t parent;
} lzw_node;

#define LZW_MAXSYMBOLS      16384 /* 1 << (0x8e & 0x1f) */
#define LZW_RESERVEDSYMBOLS 1     /* blockmode: code 256 reserved as "clear" */
#define LZW_OUTBUF          1024

typedef struct {
    lzw_node *nodes;      /* [maxsymbols] */
    int maxsymbols;
    int reservedsymbols;
    int numsymbols;
    int prevsymbol;
    int symbolsize;
} lzw_dict;

static void lzw_clear_table(lzw_dict *d)
{
    d->numsymbols = 256 + d->reservedsymbols;
    d->prevsymbol = -1;
    /* Fixed at 9 regardless of reservedsymbols - matches LZW.c's
     * ClearLZWTable exactly (its own comment flags this as a
     * simplification that doesn't matter for StuffIt's fixed use). */
    d->symbolsize = 9;
}

static int lzw_dict_init(lzw_dict *d, int maxsymbols, int reservedsymbols,
                          const sit_allocator *alloc)
{
    d->nodes = alloc->alloc(sizeof(lzw_node) * (size_t)maxsymbols, alloc->ctx);
    if (!d->nodes) return SIT_E_NOMEM;

    d->maxsymbols = maxsymbols;
    d->reservedsymbols = reservedsymbols;

    for (int i = 0; i < 256; i++) {
        d->nodes[i].chr = (uint8_t)i;
        d->nodes[i].parent = -1;
    }
    lzw_clear_table(d);
    return SIT_OK;
}

static void lzw_dict_free(lzw_dict *d, const sit_allocator *alloc)
{
    if (d->nodes) alloc->free(d->nodes, alloc->ctx);
    d->nodes = NULL;
}

static uint8_t lzw_find_first_byte(lzw_node *nodes, int symbol)
{
    while (nodes[symbol].parent >= 0) symbol = nodes[symbol].parent;
    return nodes[symbol].chr;
}

/* Ported from NextLZWSymbol. Deviates from the reference in one way,
 * for safety against a corrupt/malicious stream: the reference still
 * sets prevsymbol=symbol (and reports success to its caller, which
 * only checks for LZWInvalidCodeError) even when the table is already
 * full and symbol==numsymbols - which would then make the output walk
 * read an uninitialized node one past the allocated array. We instead
 * report SIT_E_CORRUPT in exactly that case rather than touching it. */
static int lzw_next_symbol(lzw_dict *d, int symbol)
{
    if (d->prevsymbol < 0) {
        if (symbol >= d->numsymbols) return SIT_E_CORRUPT;
        d->prevsymbol = symbol;
        return SIT_OK;
    }

    uint8_t postfixbyte;
    if (symbol < d->numsymbols) {
        postfixbyte = lzw_find_first_byte(d->nodes, symbol);
    } else if (symbol == d->numsymbols) {
        if (d->numsymbols >= d->maxsymbols) return SIT_E_CORRUPT;
        postfixbyte = lzw_find_first_byte(d->nodes, d->prevsymbol);
    } else {
        return SIT_E_CORRUPT;
    }

    int parent = d->prevsymbol;
    d->prevsymbol = symbol;

    if (d->numsymbols < d->maxsymbols) {
        d->nodes[d->numsymbols].parent = parent;
        d->nodes[d->numsymbols].chr = postfixbyte;
        d->numsymbols++;
        if (d->numsymbols < d->maxsymbols &&
            (d->numsymbols & (d->numsymbols - 1)) == 0) d->symbolsize++;
        return SIT_OK;
    }
    return SIT_E_CORRUPT;
}

/* Ported from LZWOutputLength/LZWOutputToBuffer. `scratch` must be at
 * least d->maxsymbols bytes: each dictionary entry's parent chain is
 * at most one link longer than the chain of the entry it was built
 * from, so no chain can ever exceed the number of entries added so
 * far, which is itself bounded by maxsymbols. Returns the run length,
 * or -1 if a (would-be corrupt) parent chain runs past maxsymbols
 * links, which we check defensively instead of trusting the
 * invariant blindly. */
static int lzw_output(lzw_dict *d, uint8_t *scratch)
{
    int symbol = d->prevsymbol;
    int n = 0;
    while (symbol >= 0) {
        if (n >= d->maxsymbols) return -1;
        n++;
        symbol = d->nodes[symbol].parent;
    }

    symbol = d->prevsymbol;
    uint8_t *p = scratch + n;
    while (symbol >= 0) {
        *--p = d->nodes[symbol].chr;
        symbol = d->nodes[symbol].parent;
    }
    return n;
}

/* ------------------------------------------------------------------ */
int sit_lzw_decompress(sit_io *io, uint32_t outlen,
                        sit_sink_fn sink, void *ctx,
                        const sit_allocator *alloc)
{
    lzw_dict dict;
    int rc = lzw_dict_init(&dict, LZW_MAXSYMBOLS, LZW_RESERVEDSYMBOLS, alloc);
    if (rc != SIT_OK) return rc;

    /* Scratch buffer for reconstructing one decoded symbol's byte run
     * (see lzw_output's size-bound comment above). */
    uint8_t *scratch = alloc->alloc((size_t)LZW_MAXSYMBOLS, alloc->ctx);
    if (!scratch) { lzw_dict_free(&dict, alloc); return SIT_E_NOMEM; }

    /* outbuf (1KB) is heap-allocated alongside scratch rather than kept
     * as a stack local, for the same reason. */
    uint8_t *outbuf = alloc->alloc(LZW_OUTBUF, alloc->ctx);
    if (!outbuf) {
        alloc->free(scratch, alloc->ctx);
        lzw_dict_free(&dict, alloc);
        return SIT_E_NOMEM;
    }

    lzw_br br;
    lzw_br_init(&br, io);

    size_t outpos = 0;
    uint32_t produced = 0;
    long symbolcounter = 0;
    const int blockmode = 1; /* (0x8e & 0x80) != 0 */

    rc = SIT_OK;

    while (produced < outlen) {
        int symbol;
        for (;;) {
            int symbolsize = dict.symbolsize;
            int v = lzw_br_getbits(&br, symbolsize);
            if (v < 0) { rc = SIT_E_IO; goto done; }
            symbolcounter++;
            symbol = v;

            if (symbol == 256 && blockmode) {
                /* Clear code: skip garbage padding up to the next
                 * byte-width-code boundary (see XADCompressHandle.m's
                 * comment - the reference itself calls this dumb). */
                if (symbolcounter % 8) {
                    int skip = symbolsize * (int)(8 - (symbolcounter % 8));
                    if (lzw_br_skipbits(&br, skip) < 0) { rc = SIT_E_IO; goto done; }
                }
                lzw_clear_table(&dict);
                symbolcounter = 0;
                continue;
            }
            break;
        }

        rc = lzw_next_symbol(&dict, symbol);
        if (rc != SIT_OK) goto done;

        int n = lzw_output(&dict, scratch);
        if (n < 0) { rc = SIT_E_CORRUPT; goto done; }

        int off = 0;
        while (off < n) {
            size_t space = LZW_OUTBUF - outpos;
            size_t chunk = (size_t)(n - off);
            if (chunk > space) chunk = space;
            if ((uint32_t)chunk > outlen - produced) chunk = (size_t)(outlen - produced);
            if (chunk == 0) break; /* outlen already satisfied */

            memcpy(outbuf + outpos, scratch + off, chunk);
            outpos += chunk;
            off += (int)chunk;
            produced += (uint32_t)chunk;

            if (outpos == LZW_OUTBUF) {
                if (sink(outbuf, outpos, ctx)) { rc = SIT_E_IO; goto done; }
                outpos = 0;
            }
        }
    }

done:
    if (rc == SIT_OK && outpos) {
        if (sink(outbuf, outpos, ctx)) rc = SIT_E_IO;
    }
    alloc->free(outbuf, alloc->ctx);
    alloc->free(scratch, alloc->ctx);
    lzw_dict_free(&dict, alloc);
    return rc;
}
