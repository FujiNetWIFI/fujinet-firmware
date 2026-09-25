/*
 * ndif.h - Disk Copy 6 "NDIF" image decoder, public API.
 *
 * NDIF (`.img` files with HFS type `dimg`/`rohd`/`hdro`) stores a disk
 * image as a sequence of fixed-span chunks (zero-fill, raw, ADC- or
 * KenCode-compressed) described by a `bcem` resource in the file's
 * resource fork; the compressed/raw chunk bytes themselves live in the
 * file's data fork. This decodes that into a flat, block-addressable
 * raw image, either streamed whole (ndif_extract) or read back a few
 * blocks at a time (ndif_read_blocks) - the latter is what lets a
 * decoded NDIF image be mounted as a live media type without ever
 * holding the whole thing in memory.
 *
 * Ported from ndif2raw (https://github.com/mhjacobson/ndif2raw),
 * BSD-3-Clause, Copyright (c) 2024-2025 Individual contributors (see
 * that project's COPYRIGHT file) - specifically the `bcem`
 * header/chunk-table layout, the ADC decompressor, and the KenCode
 * decompressor from ndif2raw.c (@mhjacobson - initial ADC support;
 * @Windoze345 - checksum verification and KenCode support), plus the
 * classic Mac resource-fork layout that resourcefork.c in that project
 * reads via the (Carbon-only, and long-removed) Resource Manager -
 * this file re-implements just enough of that parsing, directly over
 * an in-memory buffer, to find the `bcem` resource ndif2raw.c looks up
 * by type. Adapted here to: report errors instead of assert()/abort(),
 * decode from a `FILE*` a chunk at a time through two allocator-owned
 * scratch buffers sized once at open (bounded memory - never buffers
 * the whole image or the whole resource fork's chunk table beyond the
 * small `bcem` resource itself), and support random-access block reads
 * in addition to whole-image streaming. AppleSingle/AppleDouble input
 * framing, the CLI's argument parsing, and its whole-file CRC-32 check
 * are ndif2raw-CLI concerns, out of scope for this library (see ndif.c's
 * top comment for the full list of what was deliberately left out).
 *
 * Pure C99, no globals, no recursion, no alloca; every scratch
 * allocation goes through the caller-supplied sit_allocator (see
 * stuffit.h), same convention as the rest of lib/stuffit.
 */
#ifndef FN_NDIF_H
#define FN_NDIF_H

#include "stuffit.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------- */
/* Error codes - a domain of their own, distinct from SIT_E_* (do not   */
/* compare/mix the two numeric spaces).                                 */
/* ------------------------------------------------------------------- */
enum {
    NDIF_OK                    = 0,
    NDIF_E_IO                  = -1,  /* short read / fseek failure on the data FILE* */
    NDIF_E_FORMAT              = -2,  /* not a well-formed resource fork, or no 'bcem' resource in it */
    NDIF_E_CORRUPT             = -3,  /* malformed bcem chunk table entry, or bad compressed chunk data */
    NDIF_E_UNSUPPORTED_CHUNK   = -4,  /* a bcem chunk type this decoder doesn't implement */
    NDIF_E_NOMEM               = -5,  /* sit_allocator returned NULL */
    NDIF_E_INVAL               = -6   /* bad argument / API misuse */
};

const char *ndif_strerror(int code);

/* ------------------------------------------------------------------- */
/* Image handle                                                         */
/*                                                                       */
/* block_count and chunk_count are the only fields callers may read;    */
/* everything after them is private state (chunk table plus the two    */
/* fixed scratch buffers allocated at ndif_open() time) reachable only  */
/* through the API below - layout may change without notice.            */
/* ------------------------------------------------------------------- */
typedef struct {
    uint32_t block_count;   /* 512-byte blocks in the decoded image */
    uint32_t chunk_count;   /* number of bcem chunks describing it (excludes the terminator entry) */

    /* ---- opaque state follows; do not access directly ---- */
    FILE *priv_data;
    sit_allocator priv_alloc;
    uint32_t priv_backing_offset;
    void *priv_chunks;          /* chunk_count entries, allocator-owned */
    uint8_t *priv_compbuf;
    size_t priv_compbuf_cap;
    uint8_t *priv_decompbuf;
    size_t priv_decompbuf_cap;
} ndif_image;

/* Parses rsrc (the file's resource fork bytes) for a 'bcem' resource
 * and, if found and well-formed, allocates the fixed compressed- and
 * decompressed-chunk scratch buffers (sized from the largest single
 * chunk's compressed size and its fixed per-chunk decoded span - see
 * ndif.c) through `a`. data is a FILE* positioned anywhere (ndif seeks
 * it explicitly on every chunk decode) over the file's *data* fork,
 * where the chunk bytes actually live; ndif does not take ownership of
 * it - caller fcloses it separately, after ndif_close().
 *
 * Returns NDIF_OK with *img filled in, or a negative NDIF_E_* code:
 * NDIF_E_INVAL (NULL img/rsrc/a, or an allocator missing alloc/free),
 * NDIF_E_FORMAT (rsrc is not a well-formed classic resource fork, or
 * has no 'bcem' resource, or the bcem header's version isn't one this
 * decoder understands), NDIF_E_UNSUPPORTED_CHUNK (the chunk table
 * names a chunk type this decoder doesn't implement), NDIF_E_NOMEM. */
int ndif_open(ndif_image *img, FILE *data, const uint8_t *rsrc, size_t rsrc_len, const sit_allocator *a);

/* Decodes the whole image (block_count * 512 bytes) in chunk order,
 * delivering each chunk's decoded span to sink in one call. Returns
 * NDIF_OK, or NDIF_E_IO (short read on `data`, or sink returned
 * nonzero), NDIF_E_CORRUPT (bad compressed data), NDIF_E_INVAL. */
int ndif_extract(ndif_image *img, int (*sink)(const uint8_t *buf, size_t n, void *ctx), void *ctx);

/* Random access: decodes whichever underlying chunk(s) cover
 * [block, block+nblocks) and copies just that byte range into out
 * (which must be at least nblocks*512 bytes). Chunks are decoded into
 * the same reusable scratch buffer allocated at ndif_open() time and
 * are not cached, so redecoding the same chunk across calls is
 * expected. Returns NDIF_E_INVAL if [block, block+nblocks) runs past
 * block_count, otherwise the same error set as ndif_extract(). */
int ndif_read_blocks(ndif_image *img, uint32_t block, uint32_t nblocks, uint8_t *out);

/* Frees the chunk table and both scratch buffers through the allocator
 * given to ndif_open() and zeroes *img. Does not touch the `data`
 * FILE* - close that separately. */
void ndif_close(ndif_image *img);

/* Quick detection: 1 if rsrc parses as a well-formed resource fork
 * containing a 'bcem' resource, 0 otherwise (including a malformed
 * resource fork - probing is not a format-validity report, just a
 * yes/no). Never touches a `data` FILE* (there isn't one to pass). */
int ndif_probe(const uint8_t *rsrc, size_t rsrc_len);

#ifdef __cplusplus
}
#endif

#endif /* FN_NDIF_H */
