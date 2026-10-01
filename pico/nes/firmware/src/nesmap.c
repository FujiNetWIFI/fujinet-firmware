/* nesmap.c -- iNES/NES 2.0 header parsing and the Tier-1 mapper table.
 *
 * Register semantics follow the nesdev wiki and were checked against MAME's
 * handlers (src/devices/bus/nes/{nxrom,mmc1,mmc3,discrete,camerica,namcot,
 * sealie,ave}.cpp), which host_test/test_nesmap.c transcribes and fuzzes
 * against this file. Bank numbers wrap modulo the image's bank count, which
 * is what the address lines of a real board do for power-of-two sizes and
 * what MAME does for the rest.
 */

#include <string.h>

#include "nesmap.h"
#include "fuji_mailbox.h"

/* ---------------- bank helpers ---------------- */

static void set_prg8(nesmap_t *m, unsigned slot, uint32_t bank)
{
    uint8_t v = (uint8_t)(bank % m->prg8);

    if (m->out.prg[slot] != v) {
        m->out.prg[slot] = v;
        m->dirty |= NESMAP_DIRTY_PRG;
    }
}

static void prg16(nesmap_t *m, unsigned slot16, uint32_t bank)
{
    uint32_t n16 = m->prg8 / 2;

    if (n16 == 0)
        n16 = 1;                         /* an 8K image: mirror it       */
    bank %= n16;
    set_prg8(m, slot16 * 2, bank * 2);
    set_prg8(m, slot16 * 2 + 1, bank * 2 + 1);
}

static void prg32(nesmap_t *m, uint32_t bank)
{
    uint32_t n32 = m->prg8 / 4;
    unsigned i;

    if (n32 == 0)
        n32 = 1;
    bank %= n32;
    for (i = 0; i < 4; i++)
        set_prg8(m, i, bank * 4 + i);
}

static void set_chr1(nesmap_t *m, unsigned slot, uint32_t bank)
{
    uint16_t v = (uint16_t)(bank % m->chr1);

    if (m->out.chr[slot] != v) {
        m->out.chr[slot] = v;
        m->dirty |= NESMAP_DIRTY_CHR;
    }
}

static void chr8(nesmap_t *m, uint32_t bank)
{
    uint32_t n8 = m->chr1 / 8;
    unsigned i;

    if (n8 == 0)
        n8 = 1;
    bank %= n8;
    for (i = 0; i < 8; i++)
        set_chr1(m, i, bank * 8 + i);
}

static void chr4(nesmap_t *m, unsigned slot4, uint32_t bank)
{
    uint32_t n4 = m->chr1 / 4;
    unsigned i;

    if (n4 == 0)
        n4 = 1;
    bank %= n4;
    for (i = 0; i < 4; i++)
        set_chr1(m, slot4 * 4 + i, bank * 4 + i);
}

static void chr2(nesmap_t *m, unsigned slot2, uint32_t bank)
{
    uint32_t n2 = m->chr1 / 2;

    if (n2 == 0)
        n2 = 1;
    bank %= n2;
    set_chr1(m, slot2 * 2, bank * 2);
    set_chr1(m, slot2 * 2 + 1, bank * 2 + 1);
}

static void set_mirror(nesmap_t *m, uint8_t mir)
{
    if (m->plan.four_screen && m->plan.mirror == NESMAP_MIR_4)
        return;                          /* wired four-screen ignores the register */
    if (m->out.mirror != mir) {
        m->out.mirror = mir;
        m->dirty |= NESMAP_DIRTY_SLOW;
    }
}

static void set_wram(nesmap_t *m, bool en, bool wp)
{
    if (m->plan.prg_ram_size == 0)
        en = false;
    if (m->out.wram_en != en || m->out.wram_wp != wp) {
        m->out.wram_en = en;
        m->out.wram_wp = wp;
        m->dirty |= NESMAP_DIRTY_SLOW;
    }
}

/* Every mapper starts from the same shape: PRG fixed to the first and last
 * 16K, CHR bank 0, header mirroring, WRAM on if the board has any. */
static void reset_common(nesmap_t *m)
{
    memset(&m->out, 0, sizeof m->out);
    m->dirty = NESMAP_DIRTY_PRG | NESMAP_DIRTY_CHR | NESMAP_DIRTY_SLOW;
    memset(m->reg, 0, sizeof m->reg);
    memset(m->bank, 0, sizeof m->bank);
    m->shift = 0;
    m->shift_n = 0;
    m->bank_sel = 0;
    m->irq_latch = m->irq_counter = 0;
    m->irq_reload = m->irq_enable = m->irq_line = false;
    m->last_write_cycle = 0;
    m->consecutive = false;

    prg16(m, 0, 0);
    prg16(m, 1, (m->prg8 / 2) ? (m->prg8 / 2) - 1 : 0);
    chr8(m, 0);
    m->out.mirror = m->plan.mirror;
    m->out.wram_en = m->plan.prg_ram_size != 0;
    m->out.wram_wp = false;
    m->out.chr_wp = m->plan.chr_ram_size == 0;   /* CHR-ROM never takes writes */
}

/* ---------------- mapper 0: NROM ---------------- */

static void nrom_reset(nesmap_t *m) { reset_common(m); }
static void nrom_write(nesmap_t *m, uint16_t a, uint8_t d) { (void)m; (void)a; (void)d; }

/* ---------------- mapper 1: MMC1 (SxROM) ---------------- */

static void mmc1_apply(nesmap_t *m)
{
    uint8_t ctrl = m->reg[0];
    uint8_t chr0 = m->reg[1], chr1 = m->reg[2];
    uint8_t prg = m->reg[3] & 0x0F;
    uint32_t hi = 0;

    /* SUROM: 512K PRG selects the upper 256K through CHR bit 4. */
    if (m->plan.prg_size > 256u * 1024)
        hi = (chr0 & 0x10) ? 16 : 0;

    switch (ctrl & 0x0C) {
    case 0x00: case 0x04:                /* 32K, low bit ignored          */
        prg32(m, (hi + (prg & 0x0E)) / 2);
        break;
    case 0x08:                           /* fix first at $8000, switch $C000 */
        prg16(m, 0, hi);
        prg16(m, 1, hi + prg);
        break;
    default:                             /* switch $8000, fix last at $C000 */
        prg16(m, 0, hi + prg);
        prg16(m, 1, hi + 15);
        break;
    }

    if (ctrl & 0x10) {                   /* two 4K banks                  */
        chr4(m, 0, chr0);
        chr4(m, 1, chr1);
    } else {
        chr8(m, chr0 >> 1);
    }

    switch (ctrl & 3) {
    case 0: set_mirror(m, NESMAP_MIR_1LO); break;
    case 1: set_mirror(m, NESMAP_MIR_1HI); break;
    case 2: set_mirror(m, NESMAP_MIR_V);   break;
    default: set_mirror(m, NESMAP_MIR_H);  break;
    }
    set_wram(m, !(m->reg[3] & 0x10), false);
}

static void mmc1_reset(nesmap_t *m)
{
    reset_common(m);
    m->reg[0] = 0x0C;                    /* PRG mode 3: last bank fixed   */
    mmc1_apply(m);
}

static void mmc1_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    /* The MMC1 ignores a write on the cycle after another write, which is
     * what makes an RMW instruction's dummy write harmless on real boards
     * (MAME: m_reg_write_enable + a scheduler synchronise). */
    if (m->consecutive)
        return;

    if (d & 0x80) {
        m->shift = 0;
        m->shift_n = 0;
        m->reg[0] |= 0x0C;
        mmc1_apply(m);
        return;
    }
    m->shift = (uint8_t)((m->shift >> 1) | ((d & 1) << 4));
    if (++m->shift_n == 5) {
        m->reg[(a >> 13) & 3] = m->shift & 0x1F;
        m->shift = 0;
        m->shift_n = 0;
        mmc1_apply(m);
    }
}

/* ---------------- mapper 2: UxROM ---------------- */

static void uxrom_reset(nesmap_t *m) { reset_common(m); }
static void uxrom_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    prg16(m, 0, d);
}

/* ---------------- mapper 3: CNROM ---------------- */

static void cnrom_reset(nesmap_t *m)
{
    reset_common(m);
    prg32(m, 0);
}
static void cnrom_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    chr8(m, d);
}

/* ---------------- mapper 4: MMC3 (TxROM); 206: Namcot 108 ---------------- */

static void mmc3_apply(nesmap_t *m, bool namcot)
{
    uint32_t last = m->prg8 ? m->prg8 - 1 : 0;
    bool prg_mode = !namcot && (m->bank_sel & 0x40);
    bool chr_inv  = !namcot && (m->bank_sel & 0x80);
    unsigned base = chr_inv ? 4 : 0;     /* where the two 2K banks land   */
    unsigned i;

    if (prg_mode) {
        set_prg8(m, 0, last - 1);
        set_prg8(m, 1, m->bank[7]);
        set_prg8(m, 2, m->bank[6]);
    } else {
        set_prg8(m, 0, m->bank[6]);
        set_prg8(m, 1, m->bank[7]);
        set_prg8(m, 2, last - 1);
    }
    set_prg8(m, 3, last);

    for (i = 0; i < 2; i++)
        chr2(m, (base + i * 2) / 2, m->bank[i] >> 1);
    for (i = 0; i < 4; i++)
        set_chr1(m, ((base + 4) & 7) + i, m->bank[2 + i]);
}

static void mmc3_reset(nesmap_t *m)
{
    reset_common(m);
    m->bank[0] = 0; m->bank[1] = 2;
    m->bank[2] = 4; m->bank[3] = 5; m->bank[4] = 6; m->bank[5] = 7;
    m->bank[6] = 0; m->bank[7] = 1;
    mmc3_apply(m, false);
}

static void mmc3_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    switch (a & 0xE001) {
    case 0x8000:
        m->bank_sel = d;
        mmc3_apply(m, false);
        break;
    case 0x8001: {
        unsigned r = m->bank_sel & 7;
        /* MMC3 has six PRG address lines: R6/R7 drop their top two bits. */
        m->bank[r] = (r >= 6) ? (d & 0x3F) : d;
        mmc3_apply(m, false);
        break;
    }
    case 0xA000:
        set_mirror(m, (d & 1) ? NESMAP_MIR_H : NESMAP_MIR_V);
        break;
    case 0xA001:
        set_wram(m, (d & 0x80) != 0, (d & 0x40) != 0);
        break;
    case 0xC000:
        m->irq_latch = d;
        break;
    case 0xC001:
        m->irq_counter = 0;
        m->irq_reload = true;
        break;
    case 0xE000:
        m->irq_enable = false;
        if (m->irq_line) {
            m->irq_line = false;
            m->dirty |= NESMAP_DIRTY_IRQ;
        }
        break;
    case 0xE001:
        m->irq_enable = true;
        break;
    default:
        break;
    }
}

static void namcot108_reset(nesmap_t *m)
{
    reset_common(m);
    m->bank[0] = 0; m->bank[1] = 2;
    m->bank[2] = 4; m->bank[3] = 5; m->bank[4] = 6; m->bank[5] = 7;
    m->bank[6] = 0; m->bank[7] = 1;
    mmc3_apply(m, true);
}

static void namcot108_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    switch (a & 0x8001) {
    case 0x8000:
        m->bank_sel = d & 7;
        break;
    case 0x8001: {
        unsigned r = m->bank_sel & 7;
        m->bank[r] = (r >= 6) ? (d & 0x0F) : (d & 0x3F);
        mmc3_apply(m, true);
        break;
    }
    default:
        break;
    }
}

/* ---------------- mapper 7: AxROM ---------------- */

static void axrom_reset(nesmap_t *m)
{
    reset_common(m);
    prg32(m, 0);
    set_mirror(m, NESMAP_MIR_1LO);
}
static void axrom_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    prg32(m, d & 0x0F);
    set_mirror(m, (d & 0x10) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
}

/* ---------------- mapper 11: Color Dreams (74x377) ---------------- */

static void cdreams_reset(nesmap_t *m)
{
    reset_common(m);
    prg32(m, 0);
}
static void cdreams_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    prg32(m, d & 0x0F);
    chr8(m, d >> 4);
}

/* ---------------- mapper 30: UNROM-512 ---------------- */

static void unrom512_reset(nesmap_t *m)
{
    reset_common(m);
    if (m->plan.four_screen)
        set_mirror(m, NESMAP_MIR_1LO);
}
static void unrom512_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    /* Bit 7 is one-screen A/B, only when the header says the PCB controls
     * mirroring (iNES flag 6 bit 3, which is "four-screen" everywhere else). */
    if (m->plan.four_screen)
        set_mirror(m, (d & 0x80) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
    prg16(m, 0, d & 0x1F);
    chr8(m, (d >> 5) & 3);
}

/* ---------------- mapper 34: BNROM / NINA-001 ---------------- */

static void bnrom_reset(nesmap_t *m)
{
    reset_common(m);
    prg32(m, 0);
}
static void bnrom_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    if (m->plan.chr_size == 0) {         /* BNROM: CHR-RAM, register at $8000+ */
        if (a >= 0x8000)
            prg32(m, d);
        return;
    }
    switch (a) {                         /* NINA-001: CHR-ROM, registers in WRAM */
    case 0x7FFD: prg32(m, d & 1); break;
    case 0x7FFE: chr4(m, 0, d & 0x0F); break;
    case 0x7FFF: chr4(m, 1, d & 0x0F); break;
    default: break;
    }
}

/* ---------------- mapper 66: GxROM ---------------- */

static void gxrom_reset(nesmap_t *m)
{
    reset_common(m);
    prg32(m, 0);
}
static void gxrom_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    (void)a;
    prg32(m, (d >> 4) & 3);
    chr8(m, d & 3);
}

/* ---------------- mapper 71: Camerica BF9093/BF9097 ---------------- */

static void camerica_reset(nesmap_t *m) { reset_common(m); }
static void camerica_write(nesmap_t *m, uint16_t a, uint8_t d)
{
    if (a >= 0xC000) {
        prg16(m, 0, d & 0x0F);
    } else if (a < 0xA000 && (m->plan.submapper == 1 || m->plan.four_screen)) {
        /* BF9097 (Fire Hawk): one-screen select at $8000-$9FFF. */
        set_mirror(m, (d & 0x10) ? NESMAP_MIR_1HI : NESMAP_MIR_1LO);
    }
}

/* ---------------- the table ---------------- */

static const nesmap_desc_t descs[] = {
    {   0, "NROM",        nrom_reset,      nrom_write,      0x8000, false, NESMAP_IRQ_NONE },
    {   1, "MMC1",        mmc1_reset,      mmc1_write,      0x8000, false, NESMAP_IRQ_NONE },
    {   2, "UxROM",       uxrom_reset,     uxrom_write,     0x8000, false, NESMAP_IRQ_NONE },
    {   3, "CNROM",       cnrom_reset,     cnrom_write,     0x8000, false, NESMAP_IRQ_NONE },
    {   4, "MMC3",        mmc3_reset,      mmc3_write,      0x8000, false, NESMAP_IRQ_A12  },
    {   7, "AxROM",       axrom_reset,     axrom_write,     0x8000, false, NESMAP_IRQ_NONE },
    {  11, "ColorDreams", cdreams_reset,   cdreams_write,   0x8000, false, NESMAP_IRQ_NONE },
    {  30, "UNROM-512",   unrom512_reset,  unrom512_write,  0x8000, false, NESMAP_IRQ_NONE },
    {  34, "BNROM/NINA",  bnrom_reset,     bnrom_write,     0x6000, false, NESMAP_IRQ_NONE },
    {  66, "GxROM",       gxrom_reset,     gxrom_write,     0x8000, false, NESMAP_IRQ_NONE },
    {  71, "Camerica",    camerica_reset,  camerica_write,  0x8000, false, NESMAP_IRQ_NONE },
    { 206, "Namcot108",   namcot108_reset, namcot108_write, 0x8000, false, NESMAP_IRQ_NONE },
};

const nesmap_desc_t *nesmap_find(uint16_t mapper)
{
    unsigned i;

    for (i = 0; i < sizeof descs / sizeof descs[0]; i++)
        if (descs[i].mapper == mapper)
            return &descs[i];
    return NULL;
}

/* ---------------- the header ---------------- */

uint8_t nesmap_gate(uint32_t size)
{
    if (size > NESMAP_IMAGE_MAX)
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

/* NES 2.0 exponent-multiplier form: nibble $F means 2^E * (MM*2+1). */
static uint32_t nes2_size(uint32_t units, uint32_t unit_bytes, uint8_t msb)
{
    if (msb == 0x0F) {
        uint32_t e = units >> 2, mm = units & 3;
        if (e > 30)
            return 0xFFFFFFFFu;
        return (1u << e) * (mm * 2 + 1);
    }
    return ((uint32_t)msb << 8 | units) * unit_bytes;
}

nesmap_err_t nesmap_plan(const uint8_t *img, uint32_t size, nesmap_plan_t *p)
{
    bool nes2;
    uint32_t prg, chr;

    memset(p, 0, sizeof *p);
    if (size < NESMAP_HDR_LEN)
        return NESMAP_EEMPTY;
    if (memcmp(img, "NES\x1A", 4) != 0)
        return NESMAP_EMAGIC;
    if (img[6] & 0x04)
        return NESMAP_ETRAINER;

    nes2 = (img[7] & 0x0C) == 0x08;
    p->mirror = (img[6] & 1) ? NESMAP_MIR_V : NESMAP_MIR_H;
    p->battery = (img[6] & 2) != 0;
    p->four_screen = (img[6] & 8) != 0;
    p->mapper = img[6] >> 4;

    if (nes2) {
        p->mapper |= (uint16_t)(img[7] & 0xF0);
        p->mapper |= (uint16_t)(img[8] & 0x0F) << 8;
        p->submapper = img[8] >> 4;
        prg = nes2_size(img[4], 16384, img[9] & 0x0F);
        chr = nes2_size(img[5], 8192, img[9] >> 4);
        p->prg_ram_size = (img[10] & 0x0F) ? 64u << (img[10] & 0x0F) : 0;
        if (img[10] >> 4)
            p->prg_ram_size += 64u << (img[10] >> 4);   /* NVRAM counts too */
        p->chr_ram_size = (chr == 0 && (img[11] & 0x0F)) ? 64u << (img[11] & 0x0F) : 0;
    } else {
        /* iNES 1.0. A "DiskDude!" signature in bytes 12-15 means the upper
         * mapper nibble is garbage; MAME and every emulator mask it. */
        bool dirty = img[12] | img[13] | img[14] | img[15];
        if (!dirty)
            p->mapper |= (uint16_t)(img[7] & 0xF0);
        prg = (uint32_t)img[4] * 16384;
        chr = (uint32_t)img[5] * 8192;
        p->prg_ram_size = img[8] ? (uint32_t)img[8] * 8192 : 8192;
        p->chr_ram_size = 0;
    }

    if (chr == 0 && p->chr_ram_size == 0)
        p->chr_ram_size = (p->mapper == 30) ? 32768 : 8192;
    if (p->chr_ram_size > NESMAP_CHR_MAX)
        p->chr_ram_size = NESMAP_CHR_MAX;
    if (p->prg_ram_size > NESMAP_WRAM_MAX)
        p->prg_ram_size = NESMAP_WRAM_MAX;
    if (p->mapper == 0 && !nes2)
        p->prg_ram_size = 8192;          /* NROM + WRAM is the homebrew norm */
    if (p->four_screen && p->mapper != 30 && p->mapper != 71)
        p->mirror = NESMAP_MIR_4;

    if (prg == 0 || prg > NESMAP_PRG_MAX || chr > NESMAP_CHR_MAX)
        return NESMAP_ETOOBIG;
    p->prg_size = prg;
    p->chr_size = chr;
    p->image_size = NESMAP_HDR_LEN + prg + chr;
    if (size < p->image_size)
        return NESMAP_ETRUNC;

    p->claims_5000 = false;
    {
        const nesmap_desc_t *d = nesmap_find(p->mapper);
        if (d == NULL)
            return NESMAP_EMAPPER;
        p->claims_5000 = d->claims_5000;
    }
    p->fuji_claim = memcmp(img + NESMAP_HDR_LEN + prg - FN_PRG_CLAIM_FROM_END,
                           FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    return NESMAP_OK;
}

/* ---------------- the engine ---------------- */

nesmap_err_t nesmap_init(nesmap_t *m, const nesmap_plan_t *plan)
{
    memset(m, 0, sizeof *m);
    m->plan = *plan;
    m->desc = nesmap_find(plan->mapper);
    if (m->desc == NULL)
        return NESMAP_EMAPPER;
    m->prg8 = plan->prg_size / 8192;
    if (m->prg8 == 0)
        m->prg8 = 1;
    m->chr1 = (plan->chr_size ? plan->chr_size : plan->chr_ram_size) / 1024;
    if (m->chr1 == 0)
        m->chr1 = 8;
    nesmap_reset(m);
    return NESMAP_OK;
}

void nesmap_reset(nesmap_t *m)
{
    m->desc->reset(m);
}

void nesmap_write(nesmap_t *m, uint16_t addr, uint8_t data, uint32_t cycle)
{
    if (addr < m->desc->write_lo)
        return;
    m->consecutive = (cycle == m->last_write_cycle + 1);
    m->last_write_cycle = cycle;
    m->desc->write(m, addr, data);
}

/* nesdev, "MMC3 rev B": on a filtered A12 rise, reload when the counter is
 * zero or a reload is pending, else decrement; assert when it reaches zero
 * with interrupts enabled. Deliberately does not touch m->dirty: on the cart
 * this runs in core0's PIO interrupt while core1 owns dirty. The caller acts
 * on the return value. */
bool nesmap_a12_clock(nesmap_t *m)
{
    if (m->desc->irq != NESMAP_IRQ_A12)
        return m->irq_line;
    if (m->irq_counter == 0 || m->irq_reload) {
        m->irq_counter = m->irq_latch;
        m->irq_reload = false;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0 && m->irq_enable)
        m->irq_line = true;
    return m->irq_line;
}
