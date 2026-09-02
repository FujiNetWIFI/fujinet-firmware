/* test_o2map.c -- desktop regression gate for the cartridge image mapper.
 *
 * Build:
 *   gcc -Wall -Wextra -I../include -o /tmp/test_o2map \
 *       test_o2map.c ../src/o2map.c
 * Run with no arguments for the table-driven checks, or pass real cartridge
 * images to have their layout decoded and their reset vector sanity-checked.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "o2map.h"

static int failures;

static void check(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void plan_case(uint32_t size, o2map_err_t want_err,
                      unsigned want_banks, unsigned want_bytes)
{
    o2map_plan_t p;
    o2map_err_t e = o2map_plan(size, &p);
    char buf[96];

    snprintf(buf, sizeof buf, "%6u bytes -> %s", size, o2map_strerror(want_err));
    if (e != want_err) {
        check(buf, 0);
        return;
    }
    if (want_err != O2MAP_OK) {
        check(buf, 1);
        return;
    }
    snprintf(buf, sizeof buf, "%6u bytes -> %u x %uK bank(s)",
             size, want_banks, want_bytes / 1024);
    check(buf, p.nbanks == want_banks && p.bank_bytes == want_bytes);
}

int main(int argc, char **argv)
{
    static uint8_t banks[O2MAP_BANKS][O2MAP_BANK_BYTES];
    o2map_plan_t p;
    int i;

    puts("o2map: sizes");
    plan_case(2048,  O2MAP_OK, 1, 2048);   /* the classic 2K cart            */
    plan_case(3072,  O2MAP_OK, 1, 3072);   /* 3K, using A10                  */
    plan_case(4096,  O2MAP_OK, 2, 2048);   /* K.C. Munchkin and friends      */
    plan_case(6144,  O2MAP_OK, 2, 3072);   /* 3K wins: 6144 divides by both  */
    plan_case(8192,  O2MAP_OK, 4, 2048);
    plan_case(12288, O2MAP_OK, 4, 3072);
    plan_case(16384, O2MAP_OK, 8, 2048);
    plan_case(0,     O2MAP_ENOTCART, 0, 0);
    plan_case(1500,  O2MAP_ENOTCART, 0, 0);
    plan_case(1024,  O2MAP_ENOMAP,   0, 0);   /* 1K is neither bank size     */
    plan_case(32768, O2MAP_ETOOBIG,  0, 0);   /* 16 x 2K: more than we select*/

    puts("o2map: bank ordering");
    o2map_plan(4096, &p);
    check("bank 0 comes from the LAST chunk of the file",
          o2map_file_chunk(&p, 0) == 1);
    check("bank 1 comes from the first chunk",
          o2map_file_chunk(&p, 1) == 0);
    o2map_plan(3072, &p);
    check("a single-bank image maps chunk 0 to bank 0",
          o2map_file_chunk(&p, 0) == 0);

    puts("o2map: layout");
    {
        static uint8_t img[4096];
        memset(img, 0xAA, 2048);          /* chunk 0 */
        memset(img + 2048, 0x55, 2048);   /* chunk 1 -> must become bank 0 */
        o2map_plan(4096, &p);
        o2map_apply(img, &p, banks);
        check("bank 0 holds the boot chunk at $400",
              banks[0][O2MAP_CART_BASE] == 0x55);
        check("bank 1 holds the other chunk",
              banks[1][O2MAP_CART_BASE] == 0xAA);
        check("BIOS space below $400 is left alone",
              banks[0][0] == 0x00);
        check("missing A10 mirrors $800-$BFF into $C00-$FFF",
              memcmp(&banks[0][2048], &banks[0][3072], 1024) == 0);
        check("banks beyond the image repeat the last one",
              banks[7][O2MAP_CART_BASE] == banks[1][O2MAP_CART_BASE]);
    }

    puts("o2map: the mailbox page");
    o2map_plan(2048, &p);
    check("a 2K image still claims $F00 through the A10 mirror", !p.mailbox_ok);
    o2map_plan(3072, &p);
    check("a 3K image claims $F00 outright", !p.mailbox_ok);

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        long size;
        uint8_t *img;
        o2map_err_t e;

        if (f == NULL) {
            printf("  %-52s FAIL (cannot open)\n", argv[i]);
            failures++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        img = malloc((size_t) size);
        if (img == NULL || fread(img, (size_t) size, 1, f) != 1) {
            printf("  %-52s FAIL (cannot read)\n", argv[i]);
            failures++;
            free(img);
            fclose(f);
            continue;
        }
        fclose(f);

        e = o2map_plan((uint32_t) size, &p);
        if (e != O2MAP_OK) {
            printf("  %-52s %s\n", argv[i], o2map_strerror(e));
            failures++;
            free(img);
            continue;
        }
        o2map_apply(img, &p, banks);
        /* $400 is the cartridge reset vector: the BIOS jumps here on power-up,
         * so bank 0 must start with a JMP (opcode 0x04 | page<<5). */
        printf("  %-40s %u x %uK  reset $400 = %02X %02X  %s\n",
               argv[i], p.nbanks, p.bank_bytes / 1024,
               banks[0][O2MAP_CART_BASE], banks[0][O2MAP_CART_BASE + 1],
               ((banks[0][O2MAP_CART_BASE] & 0x1F) == 0x04) ? "JMP ok"
                                                            : "NOT A JMP");
        if ((banks[0][O2MAP_CART_BASE] & 0x1F) != 0x04)
            failures++;
        free(img);
    }

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
