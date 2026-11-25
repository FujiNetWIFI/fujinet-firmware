// Worst-case read timing at the expansion edge, from datasheet maxima and core1's
// cycle counts, for the Model I's Z80 and the speed-ups people fitted.
//
// The CPU's side: an M1 fetch drops /RD half a T-state into T1 and samples data
// at the start of T3, less its setup; the keyboard unit gates RD* (74LS32) and
// buffers it (74LS367) out to the edge, and buffers the data back in (74LS367).
// The card's side: a 74LVC input buffer, the RP2350's two-flop synchronizer, the
// longest idle chore core1 can be in when the strobe falls, the second sample,
// decode and drive, then the OE gate and the 74LVC245.
#include "check.h"

typedef struct {
    const char *name;
    int tstate_ns;
    int rd_delay;   /* clock fall to /RD fall, max */
    int data_setup; /* data valid before T3 rises, min */
} cpu_t;

static const cpu_t cpus[] = {
    {"Z80 1.774 MHz (stock)", 564, 130, 50},
    {"Z80A 2.66 MHz (speed-up)", 376, 85, 35},
    {"Z80A 3.55 MHz (speed-up)", 282, 85, 35},
};

#define LS32_NS  22
#define LS367_NS 22
#define LVC_IN_NS  7
#define LVC245_NS  7
#define GATE_NS    5

#define CORE1_MHZ 200
#define SYNC_CYCLES 2
#define CHORE_CYCLES 50 /* wd_apply taking a post, the longest of the four chores */
#define SERVE_CYCLES 35 /* second sample, decode, peek, drive: m1_core1_main's read path */

static int cycles_ns(int cycles)
{
    return cycles * 1000 / CORE1_MHZ;
}

// From RD* at the edge to the byte back at the edge, the CPU's own delays out.
static int budget_ns(const cpu_t *cpu)
{
    return cpu->tstate_ns * 3 / 2 - cpu->rd_delay - cpu->data_setup - LS32_NS - LS367_NS - LS367_NS;
}

static int card_ns(int chore_cycles)
{
    return LVC_IN_NS + cycles_ns(SYNC_CYCLES + chore_cycles + SERVE_CYCLES) + GATE_NS + LVC245_NS;
}

int main(void)
{
    for (unsigned idx = 0; idx < sizeof(cpus) / sizeof(cpus[0]); idx++)
    {
        const cpu_t *cpu = &cpus[idx];
        printf("%-26s budget %3d ns, card worst %3d, typical %3d\n", cpu->name, budget_ns(cpu),
               card_ns(CHORE_CYCLES), card_ns(0));
    }
    // The stock machine is met in the worst case with room to spare; the
    // speed-ups only when no post lands as the strobe falls.
    CHECK(card_ns(CHORE_CYCLES) + 100 <= budget_ns(&cpus[0]));
    CHECK(card_ns(0) <= budget_ns(&cpus[1]));
    return check_done("test_timing");
}
