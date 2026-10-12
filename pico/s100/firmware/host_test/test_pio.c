// The bus handshake (src/s100_bus.pio, assembled by pioasm), run on a model of
// a PIO state machine and of the card's ARM flip-flop: one service and one
// /WAIT_CLR per held cycle, DI settled before the wait ends, foreign cycles and
// decode glitches left alone.
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "s100_card.h"
#define PICO_NO_HARDWARE 1
#include "s100_bus.pio.h"

#define PSYNC (1u << S100_PIN_PSYNC)
#define WAITQ (1u << S100_PIN_WAITQ)

typedef struct {
    unsigned pc, delay;
    uint32_t isr, osr;
    unsigned isr_n;
    uint32_t rx[4];
    unsigned nrx;
    uint32_t tx;
    int have_tx;
    uint8_t di;
    int waitclr; /* pin level, idle high */
} sm_t;

static sm_t sm;
static uint32_t pins; /* what the state machine sees */
static int arm;       /* the card's ARM flip-flop */
static int decode;    /* the glue's DECODE for the current cycle */
static int clr_pulses, clr_low_cycles;
static uint8_t di_at_clr;

static void glue(void)
{
    if (!sm.waitclr)
        arm = 0;
    if (arm && decode)
        pins |= WAITQ;
    else
        pins &= ~WAITQ;
}

static void step(void)
{
    uint16_t w = s100_bus_program_instructions[sm.pc];
    unsigned op = w >> 13, next = sm.pc + 1, stall = 0;
    unsigned delay = (w >> 8) & 7;

    // A delay holds the finished instruction's side-set; pc moves on after it.
    if (sm.delay)
    {
        sm.delay--;
        goto out;
    }
    if (w & 0x1000)
    {
        int was = sm.waitclr;
        sm.waitclr = (w >> 11) & 1;
        if (was && !sm.waitclr)
        {
            clr_pulses++;
            di_at_clr = sm.di;
        }
    }
    switch (op)
    {
    case 0: /* JMP */
    {
        unsigned cond = (w >> 5) & 7;
        if (cond == 0 || (cond == 6 && (pins & WAITQ)))
            next = w & 0x1F;
        break;
    }
    case 1: /* WAIT gpio */
        if (((pins >> (w & 0x1F)) & 1) != ((w >> 7) & 1))
            stall = 1;
        break;
    case 2: /* IN pins, shift left, autopush at 22 */
    {
        unsigned n = w & 0x1F;
        sm.isr = (sm.isr << n) | (pins & ((1u << n) - 1));
        sm.isr_n += n;
        if (sm.isr_n >= 22)
        {
            sm.rx[sm.nrx++] = sm.isr;
            sm.isr = 0;
            sm.isr_n = 0;
        }
        break;
    }
    case 3: /* OUT pins, shift right */
        sm.di = (uint8_t)(sm.osr & 0xFF);
        sm.osr >>= w & 0x1F;
        break;
    case 4: /* PULL block */
        if (!sm.have_tx)
            stall = 1;
        else
        {
            sm.osr = sm.tx;
            sm.have_tx = 0;
        }
        break;
    default: /* MOV y,y: nop */
        break;
    }
    if (stall)
        return;
    sm.delay = delay;
    if (sm.pc == s100_bus_wrap && next == sm.pc + 1)
        next = s100_bus_wrap_target;
    sm.pc = next;
out:
    if (!sm.waitclr)
        clr_low_cycles++;
}

static void run(unsigned n)
{
    while (n--)
    {
        glue();
        step();
        glue();
    }
}

// One bus cycle. `ours`: the decode selects it. `glitch`: the decode flickers
// true while the address changes, before pSYNC. `answer`: core1's byte.
static void cycle(int ours, int glitch, uint8_t a, uint8_t answer)
{
    pins = (pins & ~0xFF00u) | ((uint32_t)a << 8) | (1u << S100_PIN_SINP);
    if (glitch)
    {
        decode = 1;
        run(3);
    }
    decode = ours;
    pins |= PSYNC;
    arm = 1;
    run(10);
    pins &= ~PSYNC;
    unsigned seen = sm.nrx;
    for (int t = 0; t < 400; t++)
    {
        run(1);
        if (sm.nrx > seen) // core1 answers each snapshot once
        {
            seen = sm.nrx;
            sm.tx = answer;
            sm.have_tx = 1;
        }
    }
    decode = 0;
    run(20);
}

int main(void)
{
    memset(&sm, 0, sizeof(sm));
    sm.waitclr = 1;

    // A foreign cycle: nothing taken, the wait never touched.
    cycle(0, 0, 0x42, 0);
    CHECK_EQ(sm.nrx, 0);
    CHECK_EQ(clr_pulses, 0);

    // A glitch before pSYNC on a foreign cycle: still nothing.
    cycle(0, 1, 0x42, 0);
    CHECK_EQ(sm.nrx, 0);
    CHECK_EQ(clr_pulses, 0);

    // Our read: one snapshot, the answer on DI before /WAIT_CLR, one pulse of
    // at least eight PIO cycles, and ARM cleared.
    cycle(1, 0, 0xA1, 0x5A);
    CHECK_EQ(sm.nrx, 1);
    CHECK_EQ((sm.rx[0] >> S100_PIN_A0) & 0xFF, 0xA1);
    CHECK(sm.rx[0] & (1u << S100_PIN_SINP));
    CHECK(sm.rx[0] & WAITQ);
    CHECK_EQ(clr_pulses, 1);
    CHECK_EQ(di_at_clr, 0x5A);
    CHECK(clr_low_cycles >= 8);
    CHECK_EQ(arm, 0);

    // Back to back, with a glitch first: two cycles, two services.
    cycle(1, 1, 0xA2, 0x11);
    cycle(1, 0, 0xA3, 0x22);
    CHECK_EQ(sm.nrx, 3);
    CHECK_EQ((sm.rx[2] >> S100_PIN_A0) & 0xFF, 0xA3);
    CHECK_EQ(clr_pulses, 3);
    CHECK_EQ(di_at_clr, 0x22);

    // ARM left set by a foreign cycle doesn't make the next foreign one wait.
    cycle(0, 0, 0x10, 0);
    CHECK_EQ(sm.nrx, 3);
    CHECK_EQ(clr_pulses, 3);

    return check_done("test_pio");
}
