/*
 * sit_rle90.c - RLE90 decompressor (StuffIt compression method 1).
 * Ported from XADRLE90Handle.m, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * Byte-oriented (not bit-oriented): 0x90 is an escape marker. A literal
 * 0x90 is written as "0x90 0x00". "0x90 c" with c>=2 means the byte
 * emitted immediately before the marker has a total run length of c
 * (i.e. the marker itself immediately reproduces that byte once more,
 * then queues (c-2) further repeats for subsequent pulls) - confirmed
 * against XADRLE90Handle.m's produceByteAtOffset:, which always
 * `return`s repeatedbyte on the same call that sets `count=c-2` for
 * later calls, rather than treating c-2 as the *total* new-byte count.
 * "0x90 0x01" is not a valid encoding and is treated as stream
 * corruption.
 *
 * The marker-handling state machine lives once, in sit_rle90_reader,
 * pulling its raw input through the generic sit_bytesrc callback
 * (sit_internal.h) instead of a sit_io* directly. binhex.c's BinHex
 * unwrap layer needs exactly this transform sitting on top of its own
 * 6-bit-alphabet byte decoder rather than a sit_io window, so it uses
 * sit_rle90_reader directly; sit_rle90_decompress() below is a thin
 * sink/outlen-based wrapper for the classic method-1 fork case, built
 * on the same reader over an io-backed sit_bytesrc.
 */
#include "sit_internal.h"

void sit_rle90_reader_init(sit_rle90_reader *r, sit_bytesrc src)
{
    r->src = src;
    r->last = -1;
    r->rep_left = 0;
    r->eof = 0;
    r->err = 0;
}

int sit_rle90_reader_getbyte(sit_rle90_reader *r)
{
    if (r->err) return -1;

    for (;;) {
        if (r->rep_left > 0) {
            r->rep_left--;
            return r->last;
        }
        if (r->eof) return -1;

        int c = r->src.getbyte(r->src.ctx);
        if (c < 0) { r->eof = 1; return -1; }

        if (c != 0x90) {
            r->last = c;
            return c;
        }

        int m = r->src.getbyte(r->src.ctx);
        if (m < 0) { r->err = SIT_E_IO; return -1; }

        if (m == 0) {
            r->last = 0x90;
            return 0x90;
        }
        if (m == 1) { r->err = SIT_E_CORRUPT; return -1; }

        if (r->last < 0) { r->err = SIT_E_CORRUPT; return -1; }
        r->rep_left = m - 2;   /* further repeats queued for subsequent pulls */
        return r->last;         /* this call itself produces one more copy right now */
    }
}

static int rle90_io_getbyte(void *ctx)
{
    return sit_io_getbyte((sit_io *)ctx);
}

#define SIT_RLE90_OUTBUF 512

int sit_rle90_decompress(sit_io *io, uint32_t outlen,
                          sit_sink_fn sink, void *ctx,
                          const sit_allocator *alloc)
{
    sit_bytesrc src = { rle90_io_getbyte, io };
    sit_rle90_reader rd;
    sit_rle90_reader_init(&rd, src);

    /* outbuf is heap-allocated rather than kept as a stack local, so
     * this decompressor's frame stays small on a constrained target. */
    uint8_t *outbuf = (uint8_t *)alloc->alloc(SIT_RLE90_OUTBUF, alloc->ctx);
    if (!outbuf) return SIT_E_NOMEM;

    size_t outpos = 0;
    uint32_t produced = 0;
    int rc = SIT_OK;

    while (produced < outlen) {
        int c = sit_rle90_reader_getbyte(&rd);
        if (c < 0) { rc = rd.err ? rd.err : SIT_E_IO; goto done; }

        outbuf[outpos++] = (uint8_t)c;
        produced++;

        if (outpos == SIT_RLE90_OUTBUF) {
            if (sink(outbuf, outpos, ctx)) { rc = SIT_E_IO; goto done; }
            outpos = 0;
        }
    }

    if (outpos) {
        if (sink(outbuf, outpos, ctx)) rc = SIT_E_IO;
    }

done:
    alloc->free(outbuf, alloc->ctx);
    return rc;
}
