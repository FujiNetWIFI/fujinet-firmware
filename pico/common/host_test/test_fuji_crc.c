/* test_fuji_crc.c -- CRC-32 against zlib's check values, and the sorted-table lookup. */

#include <assert.h>
#include <stdio.h>

#include "fuji_crc.h"

typedef struct {
    uint32_t crc;
    uint8_t kind, flags, ram_kb;
} row_t;

static const row_t db[] = {
    { 0x00000001u, 1, 0, 0 }, { 0x0047B615u, 2, 0, 0 }, { 0x7FFFFFFFu, 3, 1, 8 },
    { 0x80000000u, 4, 0, 0 }, { 0xCBF43926u, 5, 0, 32 }, { 0xFFFFFFFFu, 6, 0, 0 },
};
#define N (sizeof db / sizeof db[0])

static const row_t *find(unsigned n, uint32_t crc)
{
    return (const row_t *)fuji_crc_find(db, n, sizeof db[0], crc);
}

int main(void)
{
    static const uint8_t ff = 0xFF;
    const uint8_t *nine = (const uint8_t *)"123456789";
    unsigned i;

    assert(fuji_crc32(0, nine, 9) == 0xCBF43926u);
    assert(fuji_crc32(0, nine, 0) == 0);
    assert(fuji_crc32(fuji_crc32(0, nine, 4), nine + 4, 5) == 0xCBF43926u);   /* in pieces */
    assert(fuji_crc32(0, &ff, 1) == 0xFF000000u);

    for (i = 0; i < N; i++)
        assert(find(N, db[i].crc) == &db[i]);
    assert(find(N, 0) == NULL);
    assert(find(N, 0xCBF43925u) == NULL);
    assert(find(0, db[0].crc) == NULL);
    assert(find(1, db[0].crc) == &db[0] && find(1, db[1].crc) == NULL);

    printf("test_fuji_crc: all passed\n");
    return 0;
}
