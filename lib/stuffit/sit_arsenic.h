/*
 * sit_arsenic.h - Arsenic (StuffIt method 15) decompressor.
 * Ported from XADStuffItArsenicHandle.m/.h, BWT.c/BWT.h and the bit-level
 * primitives of CSInputBuffer.h/.m, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * The public entry point matches sit_internal.h exactly; this header
 * exists mainly so sit_arsenic.c can be compiled/checked standalone and
 * so a test harness can call the decompressor without pulling in every
 * other method's sit_internal.h declarations.
 */
#ifndef FN_SIT_ARSENIC_H
#define FN_SIT_ARSENIC_H

#include "stuffit.h"
#include "sit_io.h"

/* Method 15: Arsenic - range coder + adaptive order-0 models, MTF,
 * zero-run-length expansion, inverse BWT, then a final byte-oriented
 * RLE4 expansion (4 equal bytes followed by a repeat count). See
 * sit_arsenic.c for the full block-format writeup. */
int sit_arsenic_decompress(sit_io *io, uint32_t outlen,
                            sit_sink_fn sink, void *ctx,
                            const sit_allocator *alloc,
                            sit_progress *prog);

#endif /* FN_SIT_ARSENIC_H */
