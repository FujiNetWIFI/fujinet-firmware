/*
 * sit_huffman.c - static, self-describing Huffman decompressor
 * (StuffIt compression method 3). Ported from
 * XADStuffItHuffmanHandle.m, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+, using the shared
 * sit_prefixcode.c tree builder/decoder (the "commented-out simple"
 * bit-by-bit CSInputNextSymbolUsingCode(), not the table-accelerated
 * fast path - see sit_prefixcode.h).
 *
 * Bit order is MSB-first throughout (CSInputNextBit/CSInputNextBitString,
 * not the *LE variants), matching the shared sit_bitreader.h.
 *
 * No sample in this project's test corpus exercises method 3 - this
 * port is verified only by a synthetic hand-built tree + stream under
 * AddressSanitizer, NOT against real StuffIt data.
 */
#include "sit_internal.h"
#include "sit_prefixcode.h"

#define SIT_HUFFMAN_OUTBUF 1024

/* A self-describing tree over byte values 0-255 has at most 511 nodes
 * (a full binary tree with 256 leaves has 255 internal nodes). 512 is
 * a safe, generous fixed capacity. */
#define SIT_HUFFMAN_TREE_CAPACITY 512

int sit_huffman_decompress(sit_io *io, uint32_t outlen,
                            sit_sink_fn sink, void *ctx,
                            const sit_allocator *alloc)
{
    sit_bitreader br;
    sit_br_init(&br, io);

    sit_prefix_code pc;
    int rc = sit_pc_init(&pc, SIT_HUFFMAN_TREE_CAPACITY, alloc);
    if (rc != SIT_OK) return rc;

    rc = sit_pc_build_from_bitstream(&pc, &br);
    if (rc != SIT_OK) { sit_pc_free(&pc, alloc); return rc; }

    /* outbuf (1KB) is heap-allocated rather than kept as a stack local,
     * so this decompressor's frame stays small on a constrained
     * target. */
    uint8_t *outbuf = alloc->alloc(SIT_HUFFMAN_OUTBUF, alloc->ctx);
    if (!outbuf) { sit_pc_free(&pc, alloc); return SIT_E_NOMEM; }

    size_t outpos = 0;
    uint32_t produced = 0;

    while (produced < outlen) {
        int sym = sit_pc_decode(&pc, &br);
        if (sym < 0) { rc = sym; goto done; }

        outbuf[outpos++] = (uint8_t)sym;
        produced++;

        if (outpos == SIT_HUFFMAN_OUTBUF) {
            if (sink(outbuf, outpos, ctx)) { rc = SIT_E_IO; goto done; }
            outpos = 0;
        }
    }
    rc = SIT_OK;

done:
    if (rc == SIT_OK && outpos) {
        if (sink(outbuf, outpos, ctx)) rc = SIT_E_IO;
    }
    alloc->free(outbuf, alloc->ctx);
    sit_pc_free(&pc, alloc);
    return rc;
}
