/* test_fuji_cfg.c -- the .cfg sibling's mapper= line, as the carts and MAME read it. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuji_cfg.h"

static const char *parse(const char *text, unsigned chunk)
{
    static fuji_cfg_t cfg;
    unsigned off, len = (unsigned) strlen(text);

    fuji_cfg_open(&cfg);
    for (off = 0; off < len; off += chunk)
        fuji_cfg_write(&cfg, (const uint8_t *) text + off, len - off < chunk ? len - off : chunk);
    fuji_cfg_close(&cfg, false);
    return fuji_cfg_mapper(&cfg);
}

static void same(const char *got, const char *want)
{
    assert(want ? got && strcmp(got, want) == 0 : got == NULL);
}

int main(void)
{
    fuji_cfg_t cfg;
    char big[400];

    same(parse("mapper=a78_sg\n", 64), "a78_sg");
    same(parse("mapper=a78_sg", 1), "a78_sg");                  /* no newline, byte at a time */
    same(parse("\r\n  \tmapper=smsmap_janggun\r\nfoo=1\n", 3), "smsmap_janggun");
    same(parse("name=x\nmapper=y z\n", 64), "y");
    same(parse("# mapper=x\n", 64), NULL);                      /* only at a line's start */
    same(parse("foo mapper=x\n", 64), NULL);
    same(parse("mapper=\n", 64), NULL);
    same(parse("", 64), NULL);
    same(parse("mapper=abcdefghijklmnopqrstuvwxyz\n", 64), "abcdefghijklmnopqrstuvw");

    /* past the buffer: kept to 255 characters, still terminated */
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    memcpy(big, "mapper=m1\n", 10);
    same(parse(big, 100), "m1");

    fuji_cfg_open(&cfg);
    fuji_cfg_write(&cfg, (const uint8_t *) "mapper=m2\n", 10);
    fuji_cfg_close(&cfg, true);                                  /* aborted: no mapper */
    same(fuji_cfg_mapper(&cfg), NULL);

    fuji_cfg_open(&cfg);
    fuji_cfg_write(&cfg, (const uint8_t *) "mapper=m3\n", 10);
    fuji_cfg_close(&cfg, false);
    same(fuji_cfg_mapper(&cfg), "m3");
    fuji_cfg_clear(&cfg);
    same(fuji_cfg_mapper(&cfg), NULL);

    printf("test_fuji_cfg: all passed\n");
    return 0;
}
