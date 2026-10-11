/* a78plan.c -- the cart's plan for an image on stdin, for the host tools.
 *
 * Prints key=value pairs: kind, offset, size, pages, front, claim, biosok,
 * swap8k, ram, crc. tools/soak.py checks the loaded SRAM against them.
 */

#include <stdio.h>
#include <stdlib.h>

#include "a78map.h"

int main(void)
{
    static uint8_t img[A78MAP_IMAGE_MAX + 128];
    size_t len = fread(img, 1, sizeof img, stdin);
    a78map_plan_t p;
    int err = a78map_plan(img, (uint32_t)len, getenv("FUJINET_MAPPER"), &p);

    if (err != A78MAP_OK) {
        printf("error=%d\n", err);
        return 1;
    }
    printf("kind=%u offset=%u size=%u pages=%u front=%u claim=%d biosok=%u swap8k=%d"
           " ram=%u crc=0x%08X\n", p.kind, p.offset, p.size, p.pages, p.front, p.claim,
           p.biosok, p.swap8k, p.ram_size, p.crc);
    return 0;
}
