/*
 * sit_internal.h - private dispatch interface between stuffit.c (the
 * archive/entry parser) and the per-method decompressors. Not part of
 * the public API (stuffit.h); include only from the other lib/stuffit source files.
 *
 * Every decompressor:
 *  - reads exactly the compressed bytes bounded by io (sit_io_init has
 *    already been called with [data_offset, data_offset+data_comp_len));
 *  - writes exactly outlen decompressed bytes to sink, in whatever
 *    chunk sizes are convenient (a few hundred bytes to a few KB);
 *  - performs every heap allocation for scratch space (dictionaries,
 *    windows, BWT arrays, ...) through alloc, and returns SIT_E_NOMEM
 *    if alloc->alloc() returns NULL;
 *  - returns SIT_OK on success, or a negative SIT_E_* code (typically
 *    SIT_E_CORRUPT for a malformed bitstream, SIT_E_IO if the input
 *    ran out before outlen bytes were produced, SIT_E_NOMEM);
 *  - does NOT verify the entry's stored CRC-16 - stuffit.c wraps sink
 *    to do that uniformly for every method except 15 (Arsenic).
 */
#ifndef FN_SIT_INTERNAL_H
#define FN_SIT_INTERNAL_H

#include "stuffit.h"
#include "sit_io.h"

/* Generic pull-source of raw bytes: one byte (0-255) per call, or -1 at
 * EOF/error. Lets a transform (currently just RLE90) sit on top of
 * something other than a sit_io/FILE* window - specifically binhex.c's
 * 6-bit-alphabet decoder, which produces bytes one at a time with no
 * fixed underlying byte range to hand sit_io. */
typedef struct {
    int (*getbyte)(void *ctx);
    void *ctx;
} sit_bytesrc;

/* Pull-based RLE90 decoder over an arbitrary sit_bytesrc (see
 * sit_rle90.c for the marker format). Used directly by binhex.c, whose
 * layered decode (6-bit alphabet, then RLE90) has to be pulled a few
 * header bytes at a time rather than "decompress N bytes to a sink" -
 * and reused underneath the sink-based sit_rle90_decompress() below so
 * the marker-handling logic itself exists exactly once. */
typedef struct {
    sit_bytesrc src;
    int last;      /* last byte produced (literal or repeat), -1 if none yet */
    int rep_left;  /* remaining queued repeats of `last` */
    int eof;       /* underlying source exhausted cleanly (no error) */
    int err;       /* sticky SIT_E_* once set */
} sit_rle90_reader;

void sit_rle90_reader_init(sit_rle90_reader *r, sit_bytesrc src);

/* Next decoded byte, or -1 at end of input / on error - check r->err
 * (SIT_OK if the end was simply the clean end of the underlying
 * source) to tell the two apart. */
int sit_rle90_reader_getbyte(sit_rle90_reader *r);

/* Method 1: RLE90. Byte-oriented, no bit reader. Thin sink-based
 * wrapper around sit_rle90_reader over an io-backed sit_bytesrc. */
int sit_rle90_decompress(sit_io *io, uint32_t outlen,
                          sit_sink_fn sink, void *ctx,
                          const sit_allocator *alloc);

/* Method 2: classic Unix "compress"-style adaptive LZW, 9..14-bit
 * codes, as used by StuffIt (XADCompressHandle, flags 0x8e: block
 * mode + max 14 bits). MSB-first bit order via sit_bitreader. */
int sit_lzw_decompress(sit_io *io, uint32_t outlen,
                        sit_sink_fn sink, void *ctx,
                        const sit_allocator *alloc);

/* Method 3: static Huffman (XADStuffItHuffmanHandle), built on the
 * shared prefix-code helper in sit_prefixcode.c/.h. */
int sit_huffman_decompress(sit_io *io, uint32_t outlen,
                            sit_sink_fn sink, void *ctx,
                            const sit_allocator *alloc);

/* Method 13: LZ + adaptive Huffman (XADStuffIt13Handle) - the main
 * classic-format compressor. */
int sit_lzh13_decompress(sit_io *io, uint32_t outlen,
                          sit_sink_fn sink, void *ctx,
                          const sit_allocator *alloc);

/* Method 15: Arsenic - range coder + order-1 model, MTF, inverse BWT,
 * final RLE pass (XADStuffItArsenicHandle + BWT.c). prog may be NULL;
 * when non-NULL, prog->block_size is filled in once the first block
 * header is read, and prog->bytes_out is kept current. */
int sit_arsenic_decompress(sit_io *io, uint32_t outlen,
                            sit_sink_fn sink, void *ctx,
                            const sit_allocator *alloc,
                            sit_progress *prog);

#endif /* FN_SIT_INTERNAL_H */
