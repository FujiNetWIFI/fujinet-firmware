// Worst-case timing of the decode and wait-state glue against IEEE-696 and the
// CPUs S-100 machines use, from datasheet maxima at 4.5 V, -40..85 C, 50 pF.
//
// Rev0 parts: 74AHCT541 address/status buffers, 74HCT688 comparators, 74AHCT
// gates and 74AHCT74 for ARM, 74LVC07 open drain for PRDY* and PHANTOM*,
// 74ACT541 on DI. Plain 74HCT gates miss the 4 MHz Z80 budget, which the
// last block below shows.
#include "check.h"

typedef struct {
    const char *name;
    int buf, cmp, gate, ff, oc, di_oe;
} parts_t;

static const parts_t rev0 = {"Rev0 (AHCT/HCT688/LVC07)", 9, 45, 9, 10, 5, 10};
static const parts_t hct = {"all-HCT", 38, 45, 30, 44, 30, 38};

typedef struct {
    const char *name;
    int clock_ns;     /* bus state length */
    int status_valid; /* after pSYNC rises; the 8080's 8212 latch is late */
    int rdy_budget;   /* pSYNC rise to the last moment PRDY may fall */
    int dbin_rise;    /* pSYNC rise to pDBIN rise, the earliest RAM drives DI */
    int do_valid;     /* pSYNC rise to write data valid on DO */
} cpu_t;

// 8080: READY setup 120 ns before phi2 falls in T2, which leaves about 400 ns
// after pSYNC at 2 MHz; DBIN rises with phi2 of T2; data out 200 ns into T2.
// IEEE-696 masters: RDY sampled at phi rising in BS2 with 70 ns setup, which
// leaves under 180 ns at a 4 MHz Z80; pDBIN comes in BS2; scaled for 6 MHz.
static const cpu_t cpus[] = {
    {"8080 2 MHz", 500, 40, 400, 450, 700},
    {"Z80 4 MHz", 250, 0, 180, 250, 250},
    {"Z80 6 MHz", 166, 0, 120, 166, 166},
};

#define DIP_PHANTOM_OVERLAP 30 /* IEEE-696: PHANTOM* ahead of the strobe */
#define DI_ON_MAX 70           /* DI active within 70 ns of pDBIN rising */
#define DI_OFF_MAX 70
#define RP_SETTLE 50  /* PIO: pSYNC low, then this, then it reads WAITQ */
#define RP_LATE 300   /* PIO: write data first sampled this long after WAITQ */

// Address/status to PRDY* low: buffer, comparator, I/O qualify, block OR,
// AND with ARM, open drain. ARM itself is set from pSYNC through the 74.
static int prdy_ns(const parts_t *p, const cpu_t *c)
{
    int decode = c->status_valid + p->buf + p->cmp + p->gate + p->gate;
    int arm = p->ff;
    int both = decode > arm ? decode : arm;
    return both + p->gate + p->oc;
}

static int phantom_ns(const parts_t *p, const cpu_t *c)
{
    return c->status_valid + p->buf + p->cmp + p->gate + p->oc;
}

int main(void)
{
    for (unsigned i = 0; i < sizeof(cpus) / sizeof(cpus[0]); i++)
    {
        const cpu_t *c = &cpus[i];
        int prdy = prdy_ns(&rev0, c);
        int ph = phantom_ns(&rev0, c);
        int di = rev0.gate + rev0.di_oe;
        // The PIO sees WAITQ after pSYNC falls; write data must be valid by its
        // first sample and stays put while the CPU waits.
        int sample = c->clock_ns + RP_SETTLE + RP_LATE;

        printf("%-11s PRDY %3d/%3d  PHANTOM %3d/%3d  DI %2d/%2d  DO sample %3d/%3d ns\n",
               c->name, prdy, c->rdy_budget, ph, c->dbin_rise - DIP_PHANTOM_OVERLAP, di,
               DI_ON_MAX, sample, c->do_valid);
        CHECK(prdy <= c->rdy_budget);
        CHECK(ph <= c->dbin_rise - DIP_PHANTOM_OVERLAP);
        CHECK(di <= DI_ON_MAX);
        CHECK(di <= DI_OFF_MAX);
        CHECK(sample >= c->do_valid);
    }

    // Why not all-HCT: the 4 MHz Z80 budget is gone.
    int slow = prdy_ns(&hct, &cpus[1]);
    printf("%s: PRDY %d ns against %d at %s\n", hct.name, slow, cpus[1].rdy_budget, cpus[1].name);
    CHECK(slow > cpus[1].rdy_budget);
    CHECK(prdy_ns(&hct, &cpus[0]) <= cpus[0].rdy_budget);

    return check_done("test_timing");
}
