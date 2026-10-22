/* fuji_crc.c -- see fuji_crc.h. */

#include <stdbool.h>

#include "fuji_crc.h"

/* By the byte: a 1 MB image is checked while the ESP32 waits on the close. */
uint32_t fuji_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    static uint32_t table[256];
    static bool ready;
    uint32_t i;

    if (!ready) {
        for (i = 0; i < 256; i++) {
            uint32_t c = i;
            int k;

            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

const void *fuji_crc_find(const void *db, unsigned n, size_t stride, uint32_t crc)
{
    const uint8_t *base = (const uint8_t *)db;
    unsigned lo = 0, hi = n;

    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        const uint8_t *row = base + (size_t)mid * stride;
        uint32_t c = *(const uint32_t *)row;

        if (c == crc)
            return row;
        if (c < crc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}
