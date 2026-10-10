/* smsplan.c -- print the cart's plan for image files: what the soak expects
 * the cart to choose. Built by tools/soak.py from the firmware's smsmap.c. */

#include <stdio.h>
#include <stdlib.h>

#include "smsmap.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        static uint8_t buf[0x110000];
        size_t n;
        smsmap_plan_t p;
        int err;

        if (!f) { perror(argv[i]); return 1; }
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        err = smsmap_plan(buf, (uint32_t)n, NULL, &p);
        printf("%d kind=%s crc=%08X size=%u ram=%u claim=%d\n", err,
               smsmap_kind_name(p.kind), p.crc, p.size, p.ram_size, p.claim);
    }
    return 0;
}
