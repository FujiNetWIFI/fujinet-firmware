/* s2plan.c -- the host build of the cart's planner: the 64K view an image
 * gives the console, in the format the MAME device's $FUJINET_VIEWDUMP writes
 * (unclaimed bytes $FF, raster pages 0). Tier C compares the two.
 *
 *   s2plan image.st2 > view.bin
 */

#include <stdio.h>
#include <string.h>

#include "s2map.h"

static uint8_t buf[S2MAP_BUF_MAX + 1];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t raster[FN_RASTER_SIZE];

int main(int argc, char **argv)
{
    s2map_plan_t p;
    s2_view_t v;
    uint32_t len, a;
    FILE *f;
    int err;

    if (argc != 2 || !(f = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: s2plan image\n");
        return 2;
    }
    len = (uint32_t)fread(buf, 1, sizeof buf, f);
    fclose(f);
    err = s2map_plan(buf, len, false, &p);
    if (err != S2MAP_OK) {
        fprintf(stderr, "s2plan: %s refused (%d)\n", argv[1], err);
        return 1;
    }
    s2map_layout(buf, &p);
    s2map_view_init(&v, buf, &p, arena, raster, p.claim);
    for (a = 0; a < 0x10000; a++) {
        uint8_t ty = v.type[a >> 8];

        putchar(ty == S2PG_NONE ? 0xFF : ty == S2PG_RASTER ? 0 : v.page[a >> 8][a & 0xFF]);
    }
    return 0;
}
