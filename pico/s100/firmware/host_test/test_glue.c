// The decode and ARM/PRDY equations of s100_glue.h, exhaustively over address,
// status and every block base.
#include "check.h"
#include "s100_glue.h"

static int io_cases, mem_cases;

static void io_sweep(const glue_cfg_t *g)
{
    for (int a = 0; a < 256; a++)
        for (int st = 0; st < 16; st++)
        {
            glue_cycle_t c = {st & 1, (st >> 1) & 1, false, (st >> 2) & 1, (uint16_t)(0x5A00 | a)};
            if (c.sinp && c.sout)
                continue;
            glue_out_t o = glue_decode(g, &c);
            bool io = (c.sinp || c.sout) && !c.sinta;
            bool want_fdc = io && g->fdc_en && (a & 0xFC) == 0x08;
            bool want_pipe = io && (a & 0xFC) == (g->pipe_base & 0xFC);
            bool want_sio = io && g->sio_en && (a & 0xFC) == (g->sio_base & 0xFC);
            CHECK_EQ(o.decode, want_fdc || want_pipe || want_sio);
            if (o.decode)
                CHECK_EQ(o.blk, want_fdc ? S100_BLK_FDC : want_pipe ? S100_BLK_PIPE : S100_BLK_SIO);
            CHECK(!o.phantom);
            // high address byte (Z80 puts B or A there) never matters
            c.addr = (uint16_t)(0xA500 | a);
            CHECK_EQ(glue_decode(g, &c).decode, o.decode);
            // DI is driven only on reads, and only during pDBIN
            CHECK(!glue_di_oe(&o, &c, false));
            CHECK_EQ(glue_di_oe(&o, &c, true), o.decode && c.sinp);
            io_cases++;
        }
}

static void mem_sweep(const glue_cfg_t *g)
{
    for (int hi = 0; hi < 256; hi++)
        for (int st = 0; st < 4; st++)
        {
            glue_cycle_t c = {false, false, st & 1, (st >> 1) & 1, (uint16_t)(hi << 8 | 0x42)};
            glue_out_t o = glue_decode(g, &c);
            bool want = c.smemr && !c.sinta && ((g->rom_on && hi == g->page) || g->poj_armed);
            CHECK_EQ(o.decode, want);
            if (want)
                CHECK_EQ(o.blk, S100_BLK_PAGE);
            CHECK_EQ(o.phantom, want && g->ph_en);
            // memory writes are never ours: they fall through to RAM
            glue_cycle_t w = c;
            w.smemr = false;
            CHECK(!glue_decode(g, &w).decode);
            mem_cases++;
        }
}

int main(void)
{
    // Every pipe base against a fixed SIO base, and every SIO base against the pipe.
    for (int b = 0; b < 256; b += 4)
    {
        glue_cfg_t g = {(uint8_t)b, 0x14, 0xFF, true, true, true, false, true};
        io_sweep(&g);
        g.pipe_base = 0xA0;
        g.sio_base = (uint8_t)b;
        io_sweep(&g);
    }
    // Blocks switched off decode nothing.
    glue_cfg_t off = {0xA0, 0x14, 0xFF, false, false, false, false, false};
    io_sweep(&off);
    glue_cycle_t fdcr = {true, false, false, false, 0x0008};
    CHECK(!glue_decode(&off, &fdcr).decode);

    // The page at every DIP setting, on and off, with and without POJ and PHANTOM.
    for (int p = 0; p < 256; p++)
        for (int m = 0; m < 8; m++)
        {
            glue_cfg_t g = {0xA0, 0x14, (uint8_t)p, true, true, m & 1, (m >> 1) & 1, (m >> 2) & 1};
            mem_sweep(&g);
        }

    // ARM: one wait per cycle, never re-asserted after /WAIT_CLR, and a foreign
    // cycle never waits.
    glue_cfg_t g = {0xA0, 0x14, 0xFF, true, true, true, false, true};
    glue_ff_t ff = {false};
    glue_cycle_t ours = {true, false, false, false, 0x00A1};
    glue_cycle_t theirs = {false, false, true, false, 0x1234};
    glue_out_t o = glue_decode(&g, &ours);
    glue_psync_rise(&ff);
    CHECK(glue_prdy_low(&ff, &o));
    glue_wait_clr(&ff);
    CHECK(!glue_prdy_low(&ff, &o)); // same cycle, decode still true
    glue_out_t t = glue_decode(&g, &theirs);
    glue_psync_rise(&ff);
    CHECK(!glue_prdy_low(&ff, &t));
    // ARM left set by a foreign cycle: the next cycle of ours still waits once
    glue_psync_rise(&ff);
    CHECK(glue_prdy_low(&ff, &o));
    glue_wait_clr(&ff);
    CHECK(!glue_prdy_low(&ff, &o));

    // sINTA with sINP set (some Z80 cards do this) is never ours.
    glue_cycle_t inta = {true, false, false, true, 0x00A1};
    CHECK(!glue_decode(&g, &inta).decode);

    printf("test_glue: %d I/O and %d memory cycles\n", io_cases, mem_cases);
    return check_done("test_glue");
}
