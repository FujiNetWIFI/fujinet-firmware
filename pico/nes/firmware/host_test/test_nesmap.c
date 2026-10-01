/* test_nesmap.c -- the mapper engine against MAME's own handlers.
 *
 * For every Tier-1 mapper there is a reference model below that is a
 * transcription of the corresponding MAME device's write handler
 * (src/devices/bus/nes/, BSD-3-Clause) over MAME's bank helpers, kept
 * deliberately separate from nesmap.c's own helpers. Random write sequences
 * are pushed through both and the resulting slot tables, mirroring and WRAM
 * gates compared. Header parsing gets its own cases.
 *
 * Build: see Makefile.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nesmap.h"
#include "fuji_mailbox.h"

/* ---------------- the MAME-shaped reference ---------------- */

typedef struct {
    int prg_chunks;         /* 16K units, MAME's m_prg_chunks */
    int chr_chunks;         /* 8K units (ROM or RAM)          */
    int prg_bank[4];        /* 8K bank per slot               */
    int chr_orig[8];        /* 1K bank per slot               */
    int mirroring;          /* nesmap codes                   */
    int wram_en, wram_wp;
    int has_chrrom, four_screen, submapper, prg_size;
    /* per-board state */
    int latch, count, reg[4];              /* MMC1 */
    int mmc_prg_bank[2], mmc_vrom_bank[6]; /* MMC3 */
    int mmc3_latch, prg_mode, chr_mode;
    int last_write_cycle;
} ref_t;

static void r_prg8_x(ref_t *r, int start, int bank)
{
    int n8 = r->prg_chunks * 2;
    if (n8 == 0) n8 = 1;
    r->prg_bank[start] = ((bank % n8) + n8) % n8;
}
static void r_prg16_89ab(ref_t *r, int bank)
{
    int n16 = r->prg_chunks ? r->prg_chunks : 1;
    bank = ((bank % n16) + n16) % n16;
    r_prg8_x(r, 0, bank * 2);
    r_prg8_x(r, 1, bank * 2 + 1);
}
static void r_prg16_cdef(ref_t *r, int bank)
{
    int n16 = r->prg_chunks ? r->prg_chunks : 1;
    bank = ((bank % n16) + n16) % n16;
    r_prg8_x(r, 2, bank * 2);
    r_prg8_x(r, 3, bank * 2 + 1);
}
static void r_prg32(ref_t *r, int bank)
{
    int n32 = r->prg_chunks / 2;
    if (n32 == 0) n32 = 1;
    bank = ((bank % n32) + n32) % n32;
    for (int i = 0; i < 4; i++)
        r_prg8_x(r, i, bank * 4 + i);
}
static void r_bank_chr(ref_t *r, int shift, int start, int bank)
{
    int n1 = r->chr_chunks * 8;
    int size = 1 << shift;
    int nb = n1 / size;
    if (nb == 0) nb = 1;
    bank = ((bank % nb) + nb) % nb;
    for (int i = 0; i < size; i++)
        r->chr_orig[start + i] = bank * size + i;
}
#define r_chr8(r, b)    r_bank_chr(r, 3, 0, b)
#define r_chr4_x(r, s, b) r_bank_chr(r, 2, (s) * 4, b)
#define r_chr2_x(r, s, b) r_bank_chr(r, 1, (s) * 2, b)
#define r_chr1_x(r, s, b) r_bank_chr(r, 0, s, b)

static void r_set_nt_mirroring(ref_t *r, int m)
{
    if (r->four_screen && r->mirroring == NESMAP_MIR_4)
        return;
    r->mirroring = m;
}

static void ref_common_reset(ref_t *r)
{
    r_prg16_89ab(r, 0);
    r_prg16_cdef(r, r->prg_chunks - 1);
    r_chr8(r, 0);
    r->wram_en = 1;
    r->wram_wp = 0;
}

/* nxrom.cpp nes_uxrom_device::write_h: prg16_89ab(data) */
static void ref_uxrom_write(ref_t *r, uint16_t a, uint8_t d) { (void)a; r_prg16_89ab(r, d); }
/* nes_cnrom_device::write_h: chr8(data & mask) -- masking by chunks */
static void ref_cnrom_write(ref_t *r, uint16_t a, uint8_t d) { (void)a; r_chr8(r, d); }
static void ref_cnrom_reset(ref_t *r) { ref_common_reset(r); r_prg32(r, 0); }
/* nes_axrom_device::write_h: set_nt_mirroring(BIT(data,4)?HIGH:LOW); prg32(data) */
static void ref_axrom_write(ref_t *r, uint16_t a, uint8_t d)
{
    (void)a;
    r_set_nt_mirroring(r, (d & 0x10) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
    r_prg32(r, d & 0x0F);
}
static void ref_axrom_reset(ref_t *r) { ref_common_reset(r); r_prg32(r, 0); r->mirroring = NESMAP_MIR_1LO; }
/* nes_bxrom_device::write_h: prg32(data) */
static void ref_bxrom_write(ref_t *r, uint16_t a, uint8_t d) { (void)a; r_prg32(r, d); }
/* nes_gxrom_device::write_h: prg32((data & 0x30) >> 4); chr8(data & 3) */
static void ref_gxrom_write(ref_t *r, uint16_t a, uint8_t d)
{
    (void)a;
    r_prg32(r, (d & 0x30) >> 4);
    r_chr8(r, d & 0x03);
}
static void ref_prg32_reset(ref_t *r) { ref_common_reset(r); r_prg32(r, 0); }
/* discrete.cpp nes_74x377_device::write_h: chr8(data >> 4); prg32(data & 0x0f) */
static void ref_cdreams_write(ref_t *r, uint16_t a, uint8_t d)
{
    (void)a;
    r_chr8(r, d >> 4);
    r_prg32(r, d & 0x0F);
}
/* sealie.cpp nes_unrom512_device::write_h */
static void ref_unrom512_write(ref_t *r, uint16_t a, uint8_t d)
{
    (void)a;
    if (r->four_screen)
        r_set_nt_mirroring(r, (d & 0x80) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
    r_prg16_89ab(r, d & 0x1F);
    r_chr8(r, (d >> 5) & 3);
}
static void ref_unrom512_reset(ref_t *r)
{
    ref_common_reset(r);
    if (r->four_screen)
        r->mirroring = NESMAP_MIR_1LO;
}
/* camerica.cpp nes_bf9093_device::write_h */
static void ref_bf9093_write(ref_t *r, uint16_t a, uint8_t d)
{
    int off = a - 0x8000;
    if (off >= 0x4000)
        r_prg16_89ab(r, d & 0x0F);
    else if (off < 0x2000 && (r->submapper == 1 || r->four_screen))
        r_set_nt_mirroring(r, (d & 0x10) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
}
/* ave.cpp nes_nina001_device::write_m */
static void ref_nina001_write(ref_t *r, uint16_t a, uint8_t d)
{
    if (r->has_chrrom) {
        switch (a) {
        case 0x7FFD: r_prg32(r, d & 0x01); break;
        case 0x7FFE: r_chr4_x(r, 0, d & 0x0F); break;
        case 0x7FFF: r_chr4_x(r, 1, d & 0x0F); break;
        }
    } else if (a >= 0x8000) {
        ref_bxrom_write(r, a, d);
    }
}
/* mmc1.cpp nes_sxrom_device */
static void ref_sxrom_set_prg(ref_t *r)
{
    int prg_mode = r->reg[0] & 0x0C;
    int prg_offset = (r->prg_size > 256 * 1024) ? (r->reg[1] & 0x10) : 0;
    int bank = prg_offset + (r->reg[3] & 0x0F);
    switch (prg_mode) {
    case 0x00: case 0x04:
        r_prg32(r, (prg_offset + (r->reg[3] & 0x0E)) >> 1);
        break;
    case 0x08:
        r_prg16_89ab(r, prg_offset);
        r_prg16_cdef(r, bank);
        break;
    case 0x0C:
        r_prg16_89ab(r, bank);
        r_prg16_cdef(r, prg_offset + 0x0F);
        break;
    }
}
static void ref_sxrom_set_chr(ref_t *r)
{
    if (r->reg[0] & 0x10) {
        r_chr4_x(r, 0, r->reg[1]);
        r_chr4_x(r, 1, r->reg[2]);
    } else {
        r_chr8(r, r->reg[1] >> 1);
    }
}
static void ref_sxrom_update(ref_t *r)
{
    static const int mir[4] = { NESMAP_MIR_1LO, NESMAP_MIR_1HI, NESMAP_MIR_V, NESMAP_MIR_H };
    r_set_nt_mirroring(r, mir[r->reg[0] & 3]);
    ref_sxrom_set_prg(r);
    ref_sxrom_set_chr(r);
    r->wram_en = !(r->reg[3] & 0x10);
}
static void ref_sxrom_reset(ref_t *r)
{
    ref_common_reset(r);
    r->latch = 0; r->count = 0;
    r->reg[0] = 0x0C; r->reg[1] = r->reg[2] = r->reg[3] = 0;
    r->last_write_cycle = -10;
    ref_sxrom_update(r);
}
static void ref_sxrom_write(ref_t *r, uint16_t a, uint8_t d, int cycle)
{
    /* m_reg_write_enable: a write on the very next cycle is ignored */
    if (cycle == r->last_write_cycle + 1) { r->last_write_cycle = cycle; return; }
    r->last_write_cycle = cycle;
    if (d & 0x80) {
        r->count = 0;
        r->reg[0] |= 0x0C;
        ref_sxrom_update(r);
        return;
    }
    r->latch >>= 1;
    r->latch |= (d & 1) << 4;
    r->count = (r->count + 1) % 5;
    if (!r->count) {
        int reg = (a >> 13) & 3;
        r->reg[reg] = r->latch;
        ref_sxrom_update(r);
    }
}
/* mmc3.cpp nes_txrom_device::set_prg / set_chr / txrom_write */
static void ref_txrom_set_prg(ref_t *r)
{
    int prg_flip = (r->mmc3_latch & 0x40) ? 2 : 0;
    int last = r->prg_chunks * 2 - 1;
    r_prg8_x(r, 0 ^ prg_flip, r->mmc_prg_bank[0]);
    r_prg8_x(r, 1, r->mmc_prg_bank[1]);
    r_prg8_x(r, 2 ^ prg_flip, last - 1);
    r_prg8_x(r, 3, last);
}
static void ref_txrom_set_chr(ref_t *r)
{
    int chr_page = (r->mmc3_latch & 0x80) ? 4 : 0;
    r_chr2_x(r, (chr_page + 0) / 2, r->mmc_vrom_bank[0] >> 1);
    r_chr2_x(r, (chr_page + 2) / 2, r->mmc_vrom_bank[1] >> 1);
    r_chr1_x(r, (chr_page ^ 4) + 0, r->mmc_vrom_bank[2]);
    r_chr1_x(r, (chr_page ^ 4) + 1, r->mmc_vrom_bank[3]);
    r_chr1_x(r, (chr_page ^ 4) + 2, r->mmc_vrom_bank[4]);
    r_chr1_x(r, (chr_page ^ 4) + 3, r->mmc_vrom_bank[5]);
}
static void ref_txrom_reset(ref_t *r)
{
    ref_common_reset(r);
    r->mmc3_latch = 0;
    r->mmc_prg_bank[0] = 0; r->mmc_prg_bank[1] = 1;
    r->mmc_vrom_bank[0] = 0; r->mmc_vrom_bank[1] = 2;
    r->mmc_vrom_bank[2] = 4; r->mmc_vrom_bank[3] = 5;
    r->mmc_vrom_bank[4] = 6; r->mmc_vrom_bank[5] = 7;
    ref_txrom_set_prg(r);
    ref_txrom_set_chr(r);
}
static void ref_txrom_write(ref_t *r, uint16_t a, uint8_t d)
{
    int off = a - 0x8000;
    switch (off & 0x6001) {
    case 0x0000:
        r->mmc3_latch = d;
        ref_txrom_set_prg(r);
        ref_txrom_set_chr(r);
        break;
    case 0x0001: {
        int cmd = r->mmc3_latch & 0x07;
        if (cmd < 6) { r->mmc_vrom_bank[cmd] = d; ref_txrom_set_chr(r); }
        else { r->mmc_prg_bank[cmd - 6] = d & 0x3F; ref_txrom_set_prg(r); }
        break;
    }
    case 0x2000:
        r_set_nt_mirroring(r, (d & 1) ? NESMAP_MIR_H : NESMAP_MIR_V);
        break;
    case 0x2001:
        r->wram_en = (d & 0x80) != 0;
        r->wram_wp = (d & 0x40) != 0;
        break;
    default:
        break;                                 /* IRQ regs: not bank state */
    }
}
/* namcot.cpp nes_namcot3433_device::dxrom_write (Namcot 108) */
static void ref_dxrom_reset(ref_t *r)
{
    ref_common_reset(r);
    r->mmc3_latch = 0;
    r->mmc_prg_bank[0] = 0; r->mmc_prg_bank[1] = 1;
    r->mmc_vrom_bank[0] = 0; r->mmc_vrom_bank[1] = 2;
    r->mmc_vrom_bank[2] = 4; r->mmc_vrom_bank[3] = 5;
    r->mmc_vrom_bank[4] = 6; r->mmc_vrom_bank[5] = 7;
    r->prg_mode = 0; r->chr_mode = 0;
    ref_txrom_set_prg(r);
    ref_txrom_set_chr(r);
}
static void ref_dxrom_write(ref_t *r, uint16_t a, uint8_t d)
{
    switch (a & 0x8001) {
    case 0x8000:
        r->mmc3_latch = d & 0x07;              /* no mode bits on a 108 */
        break;
    case 0x8001: {
        int cmd = r->mmc3_latch & 0x07;
        if (cmd < 6) { r->mmc_vrom_bank[cmd] = d & 0x3F; ref_txrom_set_chr(r); }
        else { r->mmc_prg_bank[cmd - 6] = d & 0x0F; ref_txrom_set_prg(r); }
        break;
    }
    }
}

/* ---------------- glue ---------------- */

typedef struct {
    int mapper;
    int prg16, chr8;        /* units in the header */
    int four_screen, submapper;
    void (*reset)(ref_t *);
    void (*write)(ref_t *, uint16_t, uint8_t);
} case_t;

static const case_t cases[] = {
    {   0,  2, 1, 0, 0, ref_common_reset,   NULL },
    {   0,  1, 0, 0, 0, ref_common_reset,   NULL },
    {   1,  8, 4, 0, 0, ref_sxrom_reset,    NULL },   /* via ref_sxrom_write */
    {   1, 16, 0, 0, 0, ref_sxrom_reset,    NULL },
    {   1, 32, 0, 0, 0, ref_sxrom_reset,    NULL },   /* SUROM 512K */
    {   2,  8, 0, 0, 0, ref_common_reset,   ref_uxrom_write },
    {   2, 16, 0, 0, 0, ref_common_reset,   ref_uxrom_write },
    {   3,  2, 4, 0, 0, ref_cnrom_reset,    ref_cnrom_write },
    {   4, 16, 16, 0, 0, ref_txrom_reset,   ref_txrom_write },
    {   4, 32, 32, 0, 0, ref_txrom_reset,   ref_txrom_write },
    {   4,  8,  0, 0, 0, ref_txrom_reset,   ref_txrom_write },   /* TxROM + CHR-RAM */
    {   7,  8,  0, 0, 0, ref_axrom_reset,   ref_axrom_write },
    {  11,  4,  4, 0, 0, ref_prg32_reset,   ref_cdreams_write },
    {  30, 32,  0, 1, 1, ref_unrom512_reset, ref_unrom512_write },
    {  30,  8,  0, 0, 0, ref_unrom512_reset, ref_unrom512_write },
    {  34,  8,  0, 0, 0, ref_prg32_reset,   ref_nina001_write },  /* BNROM */
    {  34,  4,  8, 0, 0, ref_prg32_reset,   ref_nina001_write },  /* NINA-001 */
    {  66,  8,  4, 0, 0, ref_prg32_reset,   ref_gxrom_write },
    {  71, 16,  0, 0, 0, ref_common_reset,  ref_bf9093_write },
    {  71,  8,  0, 0, 1, ref_common_reset,  ref_bf9093_write },   /* Fire Hawk */
    { 206,  8,  8, 0, 0, ref_dxrom_reset,   ref_dxrom_write },
};

static uint8_t hdr[16];
static uint8_t image[16 + 512 * 1024];

static void make_header(const case_t *c)
{
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "NES\x1A", 4);
    hdr[4] = (uint8_t)c->prg16;
    hdr[5] = (uint8_t)c->chr8;
    hdr[6] = (uint8_t)((c->mapper & 0x0F) << 4) | (c->four_screen ? 0x08 : 0);
    if (c->submapper) {                        /* NES 2.0 form */
        hdr[7] = (uint8_t)((c->mapper & 0xF0) | 0x08);
        hdr[8] = (uint8_t)(c->submapper << 4);
    } else {
        hdr[7] = (uint8_t)(c->mapper & 0xF0);
    }
}

static unsigned failures;

static void compare(const case_t *c, const ref_t *r, const nesmap_t *m, unsigned step)
{
    int i, bad = 0;
    for (i = 0; i < 4; i++)
        if (r->prg_bank[i] != m->out.prg[i]) bad = 1;
    for (i = 0; i < 8; i++)
        if (r->chr_orig[i] != m->out.chr[i]) bad = 1;
    if (r->mirroring != m->out.mirror) bad = 1;
    if ((r->wram_en != 0) != m->out.wram_en) bad = 1;
    if ((r->wram_wp != 0) != m->out.wram_wp) bad = 1;
    if (!bad)
        return;
    failures++;
    printf("FAIL mapper %d prg%d chr%d step %u:\n  ref prg %d %d %d %d chr %d %d %d %d %d %d %d %d mir %d wram %d/%d\n"
           "  got prg %d %d %d %d chr %d %d %d %d %d %d %d %d mir %d wram %d/%d\n",
           c->mapper, c->prg16, c->chr8, step,
           r->prg_bank[0], r->prg_bank[1], r->prg_bank[2], r->prg_bank[3],
           r->chr_orig[0], r->chr_orig[1], r->chr_orig[2], r->chr_orig[3],
           r->chr_orig[4], r->chr_orig[5], r->chr_orig[6], r->chr_orig[7],
           r->mirroring, r->wram_en, r->wram_wp,
           m->out.prg[0], m->out.prg[1], m->out.prg[2], m->out.prg[3],
           m->out.chr[0], m->out.chr[1], m->out.chr[2], m->out.chr[3],
           m->out.chr[4], m->out.chr[5], m->out.chr[6], m->out.chr[7],
           m->out.mirror, m->out.wram_en, m->out.wram_wp);
}

static uint32_t rng = 0x2A03C02Au;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void run_case(const case_t *c)
{
    nesmap_plan_t plan;
    nesmap_t m;
    ref_t r;
    unsigned step;
    uint32_t cycle = 100;

    make_header(c);
    memset(image, 0, sizeof image);
    memcpy(image, hdr, 16);
    assert(nesmap_plan(image, 16 + c->prg16 * 16384 + c->chr8 * 8192, &plan) == NESMAP_OK);
    assert(plan.mapper == (unsigned)c->mapper);
    assert(nesmap_init(&m, &plan) == NESMAP_OK);

    memset(&r, 0, sizeof r);
    r.prg_chunks = c->prg16;
    r.chr_chunks = c->chr8 ? c->chr8 : (int)(plan.chr_ram_size / 8192);
    r.has_chrrom = c->chr8 != 0;
    r.four_screen = c->four_screen;
    r.submapper = c->submapper;
    r.prg_size = c->prg16 * 16384;
    r.mirroring = plan.mirror;
    c->reset(&r);
    /* A NES 2.0 header with byte 10 clear has no PRG-RAM; MAME would not
     * allocate any and the cart serves none. */
    if (plan.prg_ram_size == 0)
        r.wram_en = 0;
    compare(c, &r, &m, 0);

    for (step = 1; step <= 4000; step++) {
        uint16_t a;
        uint8_t d = (uint8_t)rnd();
        int gap = (int)(rnd() % 4) + 1;        /* 1 = the next cycle */

        if (c->mapper == 34 && (rnd() & 1))
            a = (uint16_t)(0x7FFD + rnd() % 3);
        else
            a = (uint16_t)(0x8000 + (rnd() & 0x7FFF));
        cycle += (uint32_t)gap;
        if (c->mapper == 1)
            ref_sxrom_write(&r, a, d, (int)cycle);
        else if (c->write)
            c->write(&r, a, d);
        nesmap_write(&m, a, d, cycle);
        compare(c, &r, &m, step);
        if (failures > 5)
            return;
    }
}

static void test_header(void)
{
    nesmap_plan_t p;
    uint8_t h[64];

    memset(image, 0, sizeof image);
    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 2; h[5] = 1; h[6] = 0x21;           /* mapper 2, vertical, battery */
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 32768 + 8192, &p) == NESMAP_OK);
    assert(p.mapper == 2 && p.mirror == NESMAP_MIR_V && !p.battery);
    assert(p.prg_size == 32768 && p.chr_size == 8192 && p.chr_ram_size == 0);
    assert(p.image_size == 16 + 32768 + 8192);
    assert(nesmap_plan(image, 16 + 32768, &p) == NESMAP_ETRUNC);

    /* DiskDude: bytes 12-15 dirty, upper nibble must be ignored */
    h[7] = 0x40; memcpy(h + 12, "Dude", 4);
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 32768 + 8192, &p) == NESMAP_OK && p.mapper == 2);
    h[7] = 0x40; memset(h + 12, 0, 4);
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 32768 + 8192, &p) == NESMAP_OK && p.mapper == 66);
}

static void test_header2(void)
{
    nesmap_plan_t p;
    uint8_t h[16];

    /* 0x40 | 2 IS mapper 66, which exists: make it one that does not. */
    memset(image, 0, sizeof image);
    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 2; h[5] = 1; h[6] = 0x20; h[7] = 0x50;   /* mapper 82 */
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 32768 + 8192, &p) == NESMAP_EMAPPER);

    /* trainer refused */
    h[6] = 0x04; h[7] = 0;
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 512 + 32768 + 8192, &p) == NESMAP_ETRAINER);

    /* NES 2.0: mapper 30 submapper 1, 512K PRG, CHR-RAM 32K, four-screen bit */
    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 32; h[5] = 0; h[6] = 0xEB; h[7] = 0x18; h[8] = 0x10; h[11] = 0x09;   /* 64<<9 = 32K CHR-RAM */
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 512 * 1024, &p) == NESMAP_OK);
    assert(p.mapper == 30 && p.submapper == 1 && p.prg_size == 512 * 1024);
    assert(p.chr_size == 0 && p.chr_ram_size == 32768 && p.four_screen && p.battery);
    assert(p.mirror == NESMAP_MIR_V);            /* 30 keeps H/V, four_screen = PCB control */

    /* NES 2.0 exponent form: PRG = 2^15 * 3 = 96K */
    h[4] = (15 << 2) | 1; h[9] = 0x0F; h[6] = 0x20; h[7] = 0x08; h[8] = 0;
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 96 * 1024, &p) == NESMAP_OK && p.prg_size == 96 * 1024);

    /* the claim */
    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 2; h[5] = 0;
    memcpy(image, h, 16);
    memcpy(image + 16 + 32768 - 16, "FUJI", 4);
    assert(nesmap_plan(image, 16 + 32768, &p) == NESMAP_OK && p.fuji_claim && p.chr_ram_size == 8192);
    assert(p.prg_ram_size == 8192);              /* NROM homebrew gets WRAM */
    image[16 + 32768 - 16] = 'X';
    assert(nesmap_plan(image, 16 + 32768, &p) == NESMAP_OK && !p.fuji_claim);

    /* gate */
    assert(nesmap_gate(0) == 0);
    assert(nesmap_gate(NESMAP_IMAGE_MAX) == 0);
    assert(nesmap_gate(NESMAP_IMAGE_MAX + 1) == FN_BOOT_ERR_TOOBIG);

    /* too big */
    h[4] = 33;
    memcpy(image, h, 16);
    assert(nesmap_plan(image, sizeof image, &p) == NESMAP_ETOOBIG);
}

static void test_mmc3_irq(void)
{
    nesmap_plan_t p;
    nesmap_t m;
    uint8_t h[16];
    int i;

    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 8; h[5] = 8; h[6] = 0x40;
    memset(image, 0, sizeof image);
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 8 * 16384 + 8 * 8192, &p) == NESMAP_OK);
    assert(nesmap_init(&m, &p) == NESMAP_OK);

    nesmap_write(&m, 0xC000, 3, 10);     /* latch = 3 */
    nesmap_write(&m, 0xC001, 0, 20);     /* reload   */
    nesmap_write(&m, 0xE001, 0, 30);     /* enable   */
    m.dirty = 0;
    /* first clock reloads to 3, then 2, 1, 0 -> IRQ on the 4th */
    for (i = 0; i < 3; i++)
        assert(!nesmap_a12_clock(&m));
    assert(nesmap_a12_clock(&m));
    assert(m.irq_line);
    /* stays asserted until acked */
    assert(nesmap_a12_clock(&m));
    m.dirty = 0;
    nesmap_write(&m, 0xE000, 0, 40);
    assert(!m.irq_line && (m.dirty & NESMAP_DIRTY_IRQ));
    /* disabled: counter still runs, line stays low */
    for (i = 0; i < 10; i++)
        assert(!nesmap_a12_clock(&m));
}

static void test_mmc1_consecutive(void)
{
    nesmap_plan_t p;
    nesmap_t m;
    uint8_t h[16];

    memset(h, 0, sizeof h);
    memcpy(h, "NES\x1A", 4);
    h[4] = 8; h[5] = 0; h[6] = 0x10;
    memset(image, 0, sizeof image);
    memcpy(image, h, 16);
    assert(nesmap_plan(image, 16 + 8 * 16384, &p) == NESMAP_OK);
    assert(nesmap_init(&m, &p) == NESMAP_OK);

    /* five writes 3 cycles apart select PRG bank 5 */
    unsigned cyc = 100, i;
    for (i = 0; i < 5; i++, cyc += 3)
        nesmap_write(&m, 0xE000, (uint8_t)((5 >> i) & 1), cyc);
    assert(m.out.prg[0] == 10 && m.out.prg[1] == 11);
    /* an RMW's dummy write: a second write one cycle later is ignored, so
     * the shift register still needs five more real writes */
    nesmap_write(&m, 0xE000, 1, cyc); cyc++;
    nesmap_write(&m, 0xE000, 1, cyc); cyc += 3;   /* ignored */
    nesmap_write(&m, 0xE000, 1, cyc); cyc += 3;
    nesmap_write(&m, 0xE000, 0, cyc); cyc += 3;
    nesmap_write(&m, 0xE000, 0, cyc); cyc += 3;
    assert(m.out.prg[0] == 10);                   /* four counted, not five */
    nesmap_write(&m, 0xE000, 0, cyc); cyc += 3;   /* fifth: value 0b00011 = 3 */
    assert(m.out.prg[0] == 6 && m.out.prg[1] == 7);
}

int main(void)
{
    unsigned i;

    test_header();
    test_header2();
    test_mmc3_irq();
    test_mmc1_consecutive();
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
        run_case(&cases[i]);
    if (failures) {
        printf("test_nesmap: %u FAILURES\n", failures);
        return 1;
    }
    printf("test_nesmap: %u mapper cases x 4000 writes, header + IRQ + MMC1 rules: ok\n",
           (unsigned)(sizeof cases / sizeof cases[0]));
    return 0;
}
