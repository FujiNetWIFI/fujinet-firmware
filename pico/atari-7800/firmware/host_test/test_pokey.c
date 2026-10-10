/* test_pokey.c -- the cart's POKEY against MAME's.
 *
 * The polynomial counters are checked bit for bit against MAME's
 * poly_init_4_5 / poly_init_9_17 (pokey.cpp, BSD-3-Clause), transcribed
 * here with their whole state so RANDOM can be checked as MAME computes it.
 * Then the channels: a pure tone must toggle at the period AUDF and AUDCTL
 * give, and volume-only must hold its level.
 *
 * The transcribed MAME code carries this notice:
 *
 * Copyright (c) Brad Oliver, Eric Smith, Juergen Buchmueller
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokey.h"

static uint32_t mame4[15], mame5[31], mame9[511];
static uint32_t *mame17;

static void mame_poly_init_4_5(uint32_t *poly, int size)
{
    int mask = (1 << size) - 1;
    uint32_t lfsr = 0;
    int const xorbit = size - 1;
    int i;

    for (i = 0; i < mask; i++) {
        lfsr = (lfsr << 1) | (~((lfsr >> 2) ^ (lfsr >> xorbit)) & 1);
        *poly++ = lfsr & mask;
    }
}

static void mame_poly_init_9_17(uint32_t *poly, int size)
{
    const uint32_t mask = (1u << size) - 1;
    uint32_t lfsr = mask, i;

    if (size == 17) {
        for (i = 0; i < mask; i++) {
            const uint32_t in8 = ((lfsr >> 8) & 1) ^ ((lfsr >> 13) & 1);
            const uint32_t in = lfsr & 1;

            lfsr = lfsr >> 1;
            lfsr = (lfsr & 0xff7f) | (in8 << 7);
            lfsr = (in << 16) | lfsr;
            *poly++ = lfsr;
        }
    } else {
        for (i = 0; i < mask; i++) {
            const uint32_t in = (lfsr & 1) ^ ((lfsr >> 5) & 1);

            lfsr = lfsr >> 1;
            lfsr = (in << 8) | lfsr;
            *poly++ = lfsr;
        }
    }
}

static void check_polys(void)
{
    pokey_t p;
    uint32_t i;

    mame17 = malloc(POKEY_POLY17_LEN * sizeof *mame17);
    assert(mame17);
    mame_poly_init_4_5(mame4, 4);
    mame_poly_init_4_5(mame5, 5);
    mame_poly_init_9_17(mame9, 9);
    mame_poly_init_9_17(mame17, 17);
    pokey_init(&p);

    for (i = 0; i < 2 * POKEY_POLY17_LEN; i++) {
        assert(pokey_poly4(i) == (mame4[i % 15] & 1));
        assert(pokey_poly5(i) == (mame5[i % 31] & 1));
        assert(pokey_poly9(i) == (mame9[i % 511] & 1));
        assert(pokey_poly17(i) == (mame17[i % POKEY_POLY17_LEN] & 1));
    }

    /* RANDOM: MAME reads (m_poly17[i] >> 8) & $FF, or m_poly9[i] & $FF */
    for (i = 0; i < POKEY_POLY17_LEN; i += 7) {
        uint8_t want = (uint8_t)((mame17[i] >> 8) & 0xFF);

        p.audctl = 0;
        if (pokey_read(&p, POKEY_RANDOM, i) != want) {
            printf("FAIL RANDOM17 at %u: got %02X want %02X\n", i,
                   pokey_read(&p, POKEY_RANDOM, i), want);
            exit(1);
        }
    }
    for (i = 0; i < 4 * POKEY_POLY9_LEN; i++) {
        uint8_t want = (uint8_t)(mame9[i % 511] & 0xFF);

        p.audctl = 0x80;
        if (pokey_read(&p, POKEY_RANDOM, i) != want) {
            printf("FAIL RANDOM9 at %u: got %02X want %02X\n", i,
                   pokey_read(&p, POKEY_RANDOM, i), want);
            exit(1);
        }
    }
    printf("  polys and RANDOM: ok (MAME's state, bit for bit)\n");
}

/* The number of output changes in n samples of 1 machine clock each. */
static unsigned toggles(pokey_t *p, unsigned clocks)
{
    static uint8_t buf[200000];
    unsigned i, n = 0;

    assert(clocks <= sizeof buf);
    pokey_render(p, buf, clocks, 1);
    for (i = 1; i < clocks; i++)
        if (buf[i] != buf[i - 1])
            n++;
    return n;
}

static void check_tone(uint8_t audctl, unsigned audf, unsigned period)
{
    pokey_t p;
    unsigned n, want;

    pokey_init(&p);
    pokey_write(&p, POKEY_SKCTL, 3, 0);
    pokey_write(&p, POKEY_AUDCTL, audctl, 0);
    pokey_write(&p, POKEY_AUDF1, (uint8_t)audf, 0);
    pokey_write(&p, POKEY_AUDF2, (uint8_t)(audf >> 8), 0);
    /* pure tone, volume 8, on channel 1 -- or 2, the high half of a pair */
    pokey_write(&p, (audctl & 0x10) ? POKEY_AUDC2 : POKEY_AUDC1, 0xA8, 0);
    pokey_write(&p, POKEY_STIMER, 0, 0);
    n = toggles(&p, 100000);
    want = 100000 / period;
    if (n + 1 < want || n > want + 1) {
        printf("FAIL tone audctl %02X audf %u: %u toggles, want %u\n", audctl, audf, n, want);
        exit(1);
    }
}

static void check_channels(void)
{
    pokey_t p;
    static uint8_t buf[1000];
    unsigned i;

    check_tone(0x00, 9, 10 * 28);                   /* 64 kHz base, 8-bit */
    check_tone(0x01, 9, 10 * 114);                  /* 15 kHz base */
    check_tone(0x40, 9, 9 + 4);                     /* channel 1 at 1.79 MHz */
    check_tone(0x50, 0x0105, 0x0105 + 7);           /* 1+2 joined, 1.79 MHz */
    check_tone(0x10, 0x0010, (0x0010 + 1) * 28);    /* joined, 64 kHz */

    /* volume only: a constant level */
    pokey_init(&p);
    pokey_write(&p, POKEY_AUDC3, 0x17, 0);
    pokey_render(&p, buf, sizeof buf, 57);
    for (i = 0; i < sizeof buf; i++)
        assert(buf[i] == 7);

    /* silence */
    pokey_init(&p);
    pokey_render(&p, buf, sizeof buf, 57);
    for (i = 0; i < sizeof buf; i++)
        assert(buf[i] == 0);
    printf("  channels: ok\n");
}

int main(void)
{
    printf("test_pokey\n");
    check_polys();
    check_channels();
    free(mame17);
    printf("test_pokey: ok\n");
    return 0;
}
