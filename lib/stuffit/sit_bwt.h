/*
 * sit_bwt.h - inverse Burrows-Wheeler transform + move-to-front decode
 * helpers used by Arsenic (StuffIt method 15).
 * Ported from BWT.h/BWT.c, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 *
 * Only the pieces XADStuffItArsenicHandle.m actually uses are ported:
 * CalculateInverseBWT() (renamed sit_bwt_inverse_transform) and the MTF
 * decoder (ResetMTFDecoder/DecodeMTF). UnsortBWT/UnsortST4/DecodeMTFBlock/
 * DecodeM1FFNBlock are dead code for method 15 (Arsenic drives the
 * transform array and the MTF table itself, one byte/symbol at a time)
 * and are intentionally not ported.
 */
#ifndef FN_SIT_BWT_H
#define FN_SIT_BWT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Build the inverse-BWT successor vector for a block of blocklen bytes.
 * transform must point to storage for at least 3*blocklen bytes (see
 * sit_bwt_transform_get/set below); block/blocklen are the
 * BWT-transformed bytes (post-MTF-decode, i.e. post-RLE-zero-run-
 * expansion) for this block.
 *
 * After this call, decoding proceeds by starting at the block's stored
 * "primary index" and repeatedly setting index = transform[index] while
 * emitting block[index] each step - blocklen iterations reproduce the
 * pre-BWT byte sequence for this block. This is already fully iterative
 * (two linear passes over counts[256]/block[]) - no recursion, no
 * unbounded stack use, matching the reference exactly.
 */
void sit_bwt_inverse_transform(uint8_t *transform, const uint8_t *block, uint32_t blocklen);

/*
 * Packed 24-bit little-endian accessors for the transform array built by
 * sit_bwt_inverse_transform() above. Every value stored in the array is
 * an index into the same blocklen-byte block, and blocklen is at most
 * 2^24 (Arsenic's largest block size), so 3 bytes per entry are always
 * enough - this is what lets Arsenic allocate 3*blocksize bytes for the
 * transform instead of 4*blocksize (a full uint32_t per entry), which
 * matters on hardware with only a few MB of contiguous heap.
 */
static inline uint32_t sit_bwt_transform_get(const uint8_t *transform, uint32_t index)
{
    const uint8_t *p = transform + (size_t)index * 3;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline void sit_bwt_transform_set(uint8_t *transform, uint32_t index, uint32_t value)
{
    uint8_t *p = transform + (size_t)index * 3;
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
}

/* Move-to-front decoder state: table[i] is the byte currently at MTF
 * rank i. Arsenic keeps exactly one of these live per block. */
typedef struct {
    uint8_t table[256];
} sit_mtf_state;

void sit_mtf_reset(sit_mtf_state *mtf);

/* Decode one MTF-coded symbol (0-255): returns the byte at rank
 * "symbol", then moves it to rank 0, shifting ranks [0,symbol) up by
 * one - exactly DecodeMTF() in BWT.c. */
uint8_t sit_mtf_decode(sit_mtf_state *mtf, uint8_t symbol);

#endif /* FN_SIT_BWT_H */
