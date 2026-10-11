/* pokey.c -- registers and event-driven sound; see pokey.h.
 *
 * The polynomial generators are transcribed from MAME's pokey.cpp, kept here
 * as bit tables of their output so the cart needs 16K, not MAME's 512K. That
 * code carries this notice:
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

#include <string.h>

#include "pokey.h"

#define AUDCTL_POLY9   0x80
#define AUDCTL_CH1_179 0x40
#define AUDCTL_CH3_179 0x20
#define AUDCTL_CH12    0x10
#define AUDCTL_CH34    0x08
#define AUDCTL_HP1     0x04
#define AUDCTL_HP2     0x02
#define AUDCTL_15K     0x01

#define AUDC_NOTPOLY5  0x80
#define AUDC_POLY4     0x40
#define AUDC_PURE      0x20
#define AUDC_VOLONLY   0x10

static uint8_t poly4_bits[2], poly5_bits[4], poly9_bits[64], poly17_bits[16384];
static bool polys_built;

static inline bool get_bit(const uint8_t *t, uint32_t i)
{
    return (t[i >> 3] >> (i & 7)) & 1;
}

static void put_bit(uint8_t *t, uint32_t i, bool v)
{
    if (v)
        t[i >> 3] |= (uint8_t)(1u << (i & 7));
}

/* MAME pokey_device::poly_init_4_5 / poly_init_9_17; bit 0 of each state. */
static void build_polys(void)
{
    uint32_t i, lfsr;

    lfsr = 0;
    for (i = 0; i < POKEY_POLY4_LEN; i++) {
        lfsr = (lfsr << 1) | (~((lfsr >> 2) ^ (lfsr >> 3)) & 1);
        put_bit(poly4_bits, i, lfsr & 1);
    }
    lfsr = 0;
    for (i = 0; i < POKEY_POLY5_LEN; i++) {
        lfsr = (lfsr << 1) | (~((lfsr >> 2) ^ (lfsr >> 4)) & 1);
        put_bit(poly5_bits, i, lfsr & 1);
    }
    lfsr = 0x1FF;
    for (i = 0; i < POKEY_POLY9_LEN; i++) {
        uint32_t in = (lfsr & 1) ^ ((lfsr >> 5) & 1);

        lfsr = (lfsr >> 1) | (in << 8);
        put_bit(poly9_bits, i, lfsr & 1);
    }
    lfsr = 0x1FFFF;
    for (i = 0; i < POKEY_POLY17_LEN; i++) {
        uint32_t in8 = ((lfsr >> 8) & 1) ^ ((lfsr >> 13) & 1);
        uint32_t in = lfsr & 1;

        lfsr >>= 1;
        lfsr = (lfsr & 0xFF7F) | (in8 << 7);
        lfsr = (in << 16) | lfsr;
        put_bit(poly17_bits, i, lfsr & 1);
    }
    polys_built = true;
}

bool pokey_poly4(uint32_t i) { return get_bit(poly4_bits, i % POKEY_POLY4_LEN); }
bool pokey_poly5(uint32_t i) { return get_bit(poly5_bits, i % POKEY_POLY5_LEN); }
bool pokey_poly9(uint32_t i) { return get_bit(poly9_bits, i % POKEY_POLY9_LEN); }
bool pokey_poly17(uint32_t i) { return get_bit(poly17_bits, i % POKEY_POLY17_LEN); }

static void restart(pokey_t *p, uint64_t t);

void pokey_init(pokey_t *p)
{
    if (!polys_built)
        build_polys();
    memset(p, 0, sizeof *p);
    restart(p, 0);
}

static uint32_t base_clocks(const pokey_t *p)
{
    return (p->audctl & AUDCTL_15K) ? 114u : 28u;
}

/* Clocks between underflows of channel c, or 0 if it never fires alone. */
static uint32_t period(const pokey_t *p, unsigned c)
{
    uint32_t b = base_clocks(p);

    switch (c) {
    case 0:
        if (p->audctl & AUDCTL_CH12)
            return 0;
        return (p->audctl & AUDCTL_CH1_179) ? p->audf[0] + 4u : (p->audf[0] + 1u) * b;
    case 1:
        if (p->audctl & AUDCTL_CH12) {
            uint32_t f = (uint32_t)p->audf[1] << 8 | p->audf[0];

            return (p->audctl & AUDCTL_CH1_179) ? f + 7u : (f + 1u) * b;
        }
        return (p->audf[1] + 1u) * b;
    case 2:
        if (p->audctl & AUDCTL_CH34)
            return 0;
        return (p->audctl & AUDCTL_CH3_179) ? p->audf[2] + 4u : (p->audf[2] + 1u) * b;
    default:
        if (p->audctl & AUDCTL_CH34) {
            uint32_t f = (uint32_t)p->audf[3] << 8 | p->audf[2];

            return (p->audctl & AUDCTL_CH3_179) ? f + 7u : (f + 1u) * b;
        }
        return (p->audf[3] + 1u) * b;
    }
}

static void restart(pokey_t *p, uint64_t t)
{
    unsigned c;

    for (c = 0; c < 4; c++) {
        uint32_t per = period(p, c);

        p->next[c] = per ? t + per : UINT64_MAX;
    }
}

static void underflow(pokey_t *p, unsigned c, uint64_t e)
{
    uint8_t audc = p->audc[c];
    uint32_t i = (uint32_t)(e - p->poly_t0);

    if ((audc & AUDC_NOTPOLY5) || pokey_poly5(i)) {
        if (audc & AUDC_PURE)
            p->out[c] ^= 1;
        else if (audc & AUDC_POLY4)
            p->out[c] = pokey_poly4(i);
        else
            p->out[c] = (p->audctl & AUDCTL_POLY9) ? pokey_poly9(i) : pokey_poly17(i);
    }
    /* channel 3 clocks channel 1's high-pass latch, 4 clocks 2's */
    if (c == 2)
        p->hp[0] = p->out[0];
    else if (c == 3)
        p->hp[1] = p->out[1];
}

static void advance(pokey_t *p, uint64_t tend)
{
    for (;;) {
        unsigned c, first = 4;
        uint64_t e = tend + 1;
        uint32_t per;

        for (c = 0; c < 4; c++)
            if (p->next[c] < e) {
                e = p->next[c];
                first = c;
            }
        if (first == 4 || e > tend)
            break;
        underflow(p, first, e);
        per = period(p, first);
        p->next[first] = per ? e + per : UINT64_MAX;
    }
    p->t = tend;
}

static uint8_t level(const pokey_t *p)
{
    unsigned c, sum = 0;

    for (c = 0; c < 4; c++) {
        uint8_t audc = p->audc[c];
        bool on = p->out[c];

        if (c == 0 && (p->audctl & AUDCTL_CH12))
            continue;                     /* the low half of a 16-bit pair */
        if (c == 2 && (p->audctl & AUDCTL_CH34))
            continue;
        if (c == 0 && (p->audctl & AUDCTL_HP1))
            on = on ^ p->hp[0];
        if (c == 1 && (p->audctl & AUDCTL_HP2))
            on = on ^ p->hp[1];
        if ((audc & AUDC_VOLONLY) || on)
            sum += audc & 0x0F;
    }
    return (uint8_t)sum;
}

void pokey_write(pokey_t *p, unsigned reg, uint8_t v, uint64_t t)
{
    unsigned c;

    if (t > p->t)
        advance(p, t);
    switch (reg & 0x0F) {
    case POKEY_AUDF1: case POKEY_AUDF2: case POKEY_AUDF3: case POKEY_AUDF4:
        p->audf[(reg & 0x0F) >> 1] = v;
        break;
    case POKEY_AUDC1: case POKEY_AUDC2: case POKEY_AUDC3: case POKEY_AUDC4:
        p->audc[(reg & 0x0F) >> 1] = v;
        break;
    case POKEY_AUDCTL:
        p->audctl = v;
        /* a channel that was silent (joined) starts counting now */
        for (c = 0; c < 4; c++)
            if (p->next[c] == UINT64_MAX && period(p, c))
                p->next[c] = p->t + period(p, c);
        break;
    case POKEY_STIMER:
        restart(p, p->t);
        break;
    case POKEY_SKCTL:
        if ((p->skctl & 3) == 0 && (v & 3) != 0)
            p->poly_t0 = p->t;            /* the polys run from here */
        p->skctl = v;
        break;
    default:
        break;
    }
}

uint8_t pokey_read(const pokey_t *p, unsigned reg, uint64_t t)
{
    uint32_t i, m;
    uint8_t v = 0;

    switch (reg & 0x0F) {
    case POKEY_RANDOM:
        i = (uint32_t)(t - p->poly_t0);
        /* MAME reads 8 bits of the LFSR's state; those are the output bits
         * it shifted through, so they come back out of the bit tables */
        if (p->audctl & AUDCTL_POLY9) {
            /* m_poly9[i] & $FF */
            for (m = 0; m < 8; m++)
                v |= (uint8_t)(pokey_poly9(i + m) << m);
        } else {
            /* (m_poly17[i] >> 8) & $FF */
            for (m = 0; m < 8; m++)
                v |= (uint8_t)(pokey_poly17(i + POKEY_POLY17_LEN - 9 + m) << m);
        }
        return v;
    case POKEY_KBCODE:
    case POKEY_IRQST:
    case POKEY_SKSTAT:
        return 0xFF;
    default:
        return 0x00;                      /* no pots, no serial input */
    }
}

void pokey_render(pokey_t *p, uint8_t *out, unsigned n, unsigned clocks_per_sample)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        advance(p, p->t + clocks_per_sample);
        out[i] = level(p);
    }
}
