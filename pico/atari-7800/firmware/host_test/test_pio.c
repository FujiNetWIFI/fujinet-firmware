/* test_pio.c -- the slot-table state machine, modelled.
 *
 * A 30-line model of what PIO does with the words a78_pio.h builds:
 * `mov isr,null / in pins,N / in null,1 / mov pc,isr` with a left-shifting
 * ISR, `set pins` with optional side-set, and `jmp`. For every slot index and a spread of banks it checks that the
 * pins end up carrying exactly the slot word patched into that slot, and it
 * pins the encoders to hand-checked words from the RP2350 datasheet so a
 * wrong bit in a78_pio.h fails here rather than on a board.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define A78_PIO_ON_HOST 1
#include "a78_pio.h"

typedef struct {
    const uint16_t *prog;
    unsigned len;
    unsigned index_bits, side_bits;
    unsigned pc;
    uint32_t isr;
    unsigned in_pins;         /* what `in pins` sees, after the synchroniser */
    unsigned set_out, side_out;
    unsigned steps;
} sm_t;

static void sm_step(sm_t *s)
{
    uint16_t w = s->prog[s->pc];
    unsigned op = w >> 13;
    unsigned side_en = (w >> 12) & 1;
    unsigned next = s->pc + 1;

    s->steps++;
    if (side_en)
        s->side_out = (w >> (12 - s->side_bits)) & ((1u << s->side_bits) - 1u);

    switch (op) {
    case 0: {                                   /* JMP, condition always */
        assert(((w >> 5) & 7) == 0);
        next = w & 0x1F;
        break;
    }
    case 2: {                                   /* IN */
        unsigned src = (w >> 5) & 7, n = w & 0x1F;
        uint32_t v = 0;
        if (n == 0) n = 32;
        if (src == 0) v = s->in_pins & ((1u << n) - 1u);
        else if (src == 3) v = 0;
        else assert(0);
        s->isr = (s->isr << n) | v;             /* IN_SHIFTDIR = left */
        break;
    }
    case 5: {                                   /* MOV */
        unsigned dest = (w >> 5) & 7, src = w & 7;
        uint32_t v;
        assert(((w >> 3) & 3) == 0);
        if (src == 3) v = 0;
        else if (src == 6) v = s->isr;
        else { assert(0); v = 0; }
        if (dest == 6) s->isr = v;
        else if (dest == 5) next = v & 0x1F;
        else assert(0);
        break;
    }
    case 7: {                                   /* SET pins */
        assert(((w >> 5) & 7) == 0);
        s->set_out = w & 0x1F;
        break;
    }
    default:
        assert(0);
    }
    s->pc = next;
    assert(s->pc < s->len);
}

/* Run until the SM has executed a slot's `set` for the current index and
 * looped back: at most one full loop plus a slot. */
static unsigned sm_settle(sm_t *s)
{
    unsigned start = s->steps;
    unsigned seen_set = 0;

    while (s->steps - start < 40) {
        unsigned pc = s->pc;
        sm_step(s);
        if ((s->prog[pc] >> 13) == 7)
            seen_set = 1;
        if (seen_set && (s->prog[pc] >> 13) == 0)
            break;                  /* the jmp after the set */
    }
    return s->steps - start;
}

static void check_table(void)
{
    const unsigned nslots = A78_PIO_SLOTS, side_bits = A78_PIO_SIDE_BITS;
    uint16_t prog[32], words[A78_PIO_SLOTS];
    unsigned len, idx, trial, worst = 0;
    uint32_t seed = 0x1234567u;

    for (trial = 0; trial < 200; trial++) {
        for (idx = 0; idx < nslots; idx++) {
            seed = seed * 1103515245u + 12345u;
            words[idx] = (uint16_t)((seed >> 8) & 0x1FFu);
        }
        len = a78_pio_build(prog, words);
        assert(len == A78_PIO_PROG_LEN);
        for (idx = 0; idx < nslots; idx++) {
            sm_t s;
            unsigned got, steps;
            memset(&s, 0, sizeof s);
            s.prog = prog; s.len = len;
            s.index_bits = A78_PIO_INDEX_BITS; s.side_bits = side_bits;
            s.pc = 2 * nslots;                  /* start at the loop */
            s.in_pins = idx;
            s.isr = 0xFFFFFFFFu;                /* stale: mov isr,null must clear it */
            steps = sm_settle(&s);
            got = s.set_out | (s.side_out << 5);
            if (got != words[idx]) {
                printf("FAIL trial %u idx %u: word %u got %u\n", trial, idx, words[idx], got);
                assert(0);
            }
            if (steps > worst) worst = steps;
        }
        /* patching one slot must not disturb the others */
        {
            unsigned k = trial % nslots, w = (trial * 37u) & 0x1FFu;
            prog[2 * k] = a78_pio_slot_instr(w);
            assert(a78_pio_slot_word(prog[2 * k]) == w);
            words[k] = (uint16_t)w;
            for (idx = 0; idx < nslots; idx++) {
                sm_t s;
                memset(&s, 0, sizeof s);
                s.prog = prog; s.len = len;
                s.index_bits = A78_PIO_INDEX_BITS; s.side_bits = side_bits;
                s.pc = 2 * nslots; s.in_pins = idx;
                sm_settle(&s);
                assert((s.set_out | (s.side_out << 5)) == words[idx]);
            }
        }
    }
    printf("  slot table: %u slots x 200 tables ok, worst %u instructions to settle\n",
           nslots, worst);
    assert(worst <= 9);
}

static void check_encoders(void)
{
    /* Hand-checked against RP2350 datasheet 11.4 encodings. */
    assert(a78_pio_enc_jmp(16) == 0x0010);                 /* jmp 16          */
    assert(a78_pio_enc_in(0, 3) == 0x4003);                /* in pins, 3      */
    assert(a78_pio_enc_in(3, 1) == 0x4061);                /* in null, 1      */
    assert(a78_pio_enc_mov(6, 3) == 0xA0C3);               /* mov isr, null   */
    assert(a78_pio_enc_mov(5, 6) == 0xA0A6);               /* mov pc, isr     */
    assert(a78_pio_enc_set_pins(0x1F) == 0xE01F);          /* set pins, 31    */
    assert(a78_pio_enc_sideset_opt(4, 0xF) == 0x1F00);     /* side 15, 4 bits opt */
    assert(a78_pio_slot_instr(0x1FF) == 0xFF1F);           /* every bit set   */
    assert(a78_pio_slot_instr(0x020) == 0xF100);           /* A18: lo 0, hi 1 */
    assert(a78_pio_slot_instr(0x040) == 0xF200);           /* ROM_EN          */
    assert(a78_pio_slot_instr(0x100) == 0xF800);           /* A8MASK          */
    printf("  encoders: ok\n");
}

int main(void)
{
    printf("test_pio\n");
    check_encoders();
    check_table();
    printf("test_pio: ok\n");
    return 0;
}
