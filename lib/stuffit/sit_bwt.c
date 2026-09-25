/*
 * sit_bwt.c - see sit_bwt.h.
 * Ported from BWT.c, The Unarchiver
 * (https://github.com/MacPaw/XADMaster), LGPL v2.1+.
 */
#include "sit_bwt.h"

void sit_bwt_inverse_transform(uint8_t *transform, const uint8_t *block, uint32_t blocklen)
{
    /* counts[]/cumulativecounts[] are 1KB each (2KB combined) - kept as
     * function-static storage rather than stack locals so a
     * constrained caller's stack frame here stays small. Safe because
     * this function is never called reentrantly or recursively (one
     * Arsenic block is fully inverse-transformed before the next
     * begins) and both arrays are fully rewritten by this function
     * before being read, on every call. */
    static uint32_t counts[256];
    static uint32_t cumulativecounts[256];
    uint32_t i;
    int c;

    for (i = 0; i < 256; i++) counts[i] = 0;
    for (i = 0; i < blocklen; i++) counts[block[i]]++;

    uint32_t total = 0;
    for (c = 0; c < 256; c++) {
        cumulativecounts[c] = total;
        total += counts[c];
        counts[c] = 0;
    }

    for (i = 0; i < blocklen; i++) {
        uint8_t b = block[i];
        sit_bwt_transform_set(transform, cumulativecounts[b] + counts[b], i);
        counts[b]++;
    }
}

void sit_mtf_reset(sit_mtf_state *mtf)
{
    int i;
    for (i = 0; i < 256; i++) mtf->table[i] = (uint8_t)i;
}

uint8_t sit_mtf_decode(sit_mtf_state *mtf, uint8_t symbol)
{
    uint8_t res = mtf->table[symbol];
    int i;
    for (i = (int)symbol; i > 0; i--) mtf->table[i] = mtf->table[i - 1];
    mtf->table[0] = res;
    return res;
}
