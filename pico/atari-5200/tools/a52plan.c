/* a52plan.c -- the cart's plan for an image, for the host tools.
 *
 *   a52plan [file]           key=value: kind, src, offset, size, nbanks,
 *                            claim, crc, view_len (stdin if no file)
 *   a52plan --guess16k file  the 16K tracer's verdict, as a MAME slot name
 *   a52plan --view out file  the 32K window at power-on, as the CPU reads it
 *
 * FUJINET_MAPPER, if set, is the .cfg override. tools/soak.py checks a loaded
 * cart's window against --view.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a52map.h"

static uint8_t img[A52MAP_VIEW_MAX];
static uint8_t arena[FN_ARENA_SIZE];

static size_t slurp(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : stdin;
    size_t n;

    if (!f) {
        perror(path);
        exit(2);
    }
    n = fread(img, 1, A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER, f);
    if (path)
        fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    const char *view_out = NULL;
    a52map_plan_t p;
    size_t len;
    int err;

    if (argc == 3 && strcmp(argv[1], "--guess16k") == 0) {
        if (slurp(argv[2]) != 0x4000) {
            fprintf(stderr, "a52plan: %s is not 16K\n", argv[2]);
            return 1;
        }
        printf("%s\n", a52map_kind_name(a52map_guess16k(img)));
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "--view") == 0) {
        view_out = argv[2];
        argv += 2;
        argc -= 2;
    }
    len = slurp(argc > 1 ? argv[1] : NULL);
    err = a52map_plan(img, (uint32_t)len, getenv("FUJINET_MAPPER"), &p);
    if (err != A52MAP_OK) {
        printf("error=%d\n", err);
        return 1;
    }
    if (view_out) {
        a52_view_t v;
        FILE *f = fopen(view_out, "wb");
        uint32_t off;

        if (!f) {
            perror(view_out);
            return 2;
        }
        a52map_layout(img, &p);
        a52map_view_init(&v, img, &p, arena, false);
        for (off = 0; off < A52MAP_WINDOW; off++)
            fputc(a52_serve(&v, off), f);
        fclose(f);
    }
    printf("kind=%s src=%u offset=%u size=%u nbanks=%u claim=%d crc=0x%08X view_len=%u\n",
           a52map_kind_name(p.kind), p.src, p.offset, p.size, p.nbanks, p.claim, p.crc,
           p.view_len);
    return 0;
}
