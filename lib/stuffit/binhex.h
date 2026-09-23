/*
 * binhex.h - BinHex 4.0 (.hqx) unwrap layer, public API.
 *
 * BinHex is a pure ASCII transport wrapper, not a compressor of its own
 * archive format: it wraps one Mac file's data fork (and optionally its
 * resource fork) as printable text via a 6-bit alphabet, a byte-level
 * RLE90 pass underneath that, and three CRC-CCITT checksums. What we
 * care about here is unwrapping a `.hqx` to get back the raw bytes of
 * whatever it wrapped - typically a whole `.sit` archive - and handing
 * those bytes to sit_open() exactly as if they had been a bare `.sit`
 * file all along.
 *
 * Ported from XADBinHexParser.m (both the format-recognition logic in
 * +recognizeFileWithHandle:firstBytes:name: and the decode state
 * machine in XADBinHexHandle), The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+. The RLE90 pass
 * underneath the 6-bit decode reuses sit_rle90_reader (sit_internal.h)
 * rather than re-implementing XADBinHexHandle's inlined copy of the
 * same marker logic - see sit_rle90.c.
 */
#ifndef FN_BINHEX_H
#define FN_BINHEX_H

#include "stuffit.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[64];                /* NUL-terminated, 1-63 raw bytes as stored (namelen is 1..63) */
    char type[5], creator[5];     /* 4-char OSType + NUL */
    uint16_t finder_flags;
    uint32_t data_len;            /* size of the wrapped file's data fork, e.g. a whole .sit's bytes */
    uint32_t resource_len;        /* size of the wrapped file's resource fork, 0 if none - info
                                    * only, never decoded (see hqx_open()) */
    uint8_t *data;                 /* allocator-owned, data_len bytes (at least 1 byte is always
                                     * allocated even when data_len==0, so hqx_data_fork() always
                                     * gets a valid buffer pointer) */
    const sit_allocator *alloc;    /* retained so hqx_close() can free `data` */
} hqx_file;

/* Scan f (from its current position, normally offset 0) for the BinHex
 * ASCII banner within roughly the first 8KB, then decode the header and
 * the whole data fork into an allocator-owned buffer. Does not decode
 * the resource fork's bytes (its raw bytes are not needed for BinHex's
 * own purpose - the wrapped .sit's own resource-fork data, if any,
 * lives inside the .sit payload itself and goes through sit_extract's
 * fork selector once sit_open() has parsed hqx_data_fork()).
 *
 * Every checksum BinHex stores (the header CRC and the data-fork CRC,
 * both CRC-CCITT/XMODEM) is verified; a mismatch is treated as a hard
 * SIT_E_CRC error rather than a warning, matching sit_extract()'s own
 * CRC policy for every non-Arsenic method - unlike Arsenic's CRC field,
 * nothing about the BinHex format is known to make its CRCs unreliable.
 *
 * Returns SIT_OK with *h filled in, SIT_E_FORMAT if the file is not
 * BinHex-wrapped at all (no banner found), or another negative SIT_E_*
 * code: SIT_E_IO, SIT_E_CORRUPT (bad 6-bit alphabet character or a
 * malformed RLE90 marker), SIT_E_CRC, SIT_E_NOMEM. Does not take
 * ownership of f - caller fcloses it separately, whenever convenient
 * (hqx_open only needs it for the duration of this call). */
int hqx_open(FILE *f, const sit_allocator *a, hqx_file *h);

/* A read-only FILE* over h->data (fmemopen; a temporary file on Windows),
 * suitable for handing straight to sit_open(). Caller fcloses it
 * separately from hqx_close(); h must outlive it. Returns NULL on failure
 * (see errno). */
FILE *hqx_data_fork(hqx_file *h);

/* Frees h->data through the allocator retained at hqx_open() time and
 * zeroes *h. Does not touch any FILE* obtained via hqx_data_fork() -
 * close that separately, and before calling hqx_close() if you still
 * need to read from it. */
void hqx_close(hqx_file *h);

#ifdef __cplusplus
}
#endif

#endif /* FN_BINHEX_H */
