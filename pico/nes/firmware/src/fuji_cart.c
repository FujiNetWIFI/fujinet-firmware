/* fuji_cart.c -- core0's half of the cartridge: the ring drain, the '595,
 * the load sequence and the console-power watchdog.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "nes_pio.h"
#include "nes_irq.h"

fuji_ring_t fuji_ring;
uint8_t fuji_arena[FN_ARENA_SIZE];
uint8_t fuji_wram[NESMAP_WRAM_MAX];
uint8_t fuji_vectors[256];
volatile nes_serve_t fuji_serve;
nesmap_t fuji_map;
volatile uint32_t fuji_bus_cycle;
volatile bool fuji_boot_armed;
volatile bool fuji_have_staged;
volatile uint8_t fuji_slow_dirty;
volatile uint8_t fuji_irq_dirty;

static const uint8_t *resident_base;
static nesmap_plan_t resident_plan;
static const uint8_t *staged_base;
static nesmap_plan_t staged_plan;
static uint8_t sr_shadow;
static uint8_t diag_rmw;

/* The load in progress. */
static enum { LS_IDLE, LS_RUN, LS_DONE } ls_state;
static const uint8_t *ls_base;
static nesmap_plan_t ls_plan;
static uint32_t ls_slice, ls_nprg, ls_nchr;   /* 1K units */
static uint8_t ls_seq;
static bool ls_ack_pending;
static bool ls_autoload;                      /* boot the resident image when the console runs */

/* Console power: M2 stops, the SRAMs' contents are gone, CONFIG must reload. */
static uint32_t wd_last_cycle;
static absolute_time_t wd_last_change;
static bool console_alive;

/* ---------------- the '595 and the IRQ line ---------------- */

static inline void sr_delay(void)
{
    for (int i = 0; i < 12; i++)               /* ~60 ns at 200 MHz; HCT needs 20 */
        __asm volatile("nop");
}

void fuji_cart_sr_write(uint8_t bits)
{
    unsigned i;

    sr_shadow = bits;
    for (i = 0; i < 8; i++) {                  /* Q7 first, so Q0 = bit 0 */
        if (bits & 0x80)
            sio_hw->gpio_set = 1u << SR_SER_PIN;
        else
            sio_hw->gpio_clr = 1u << SR_SER_PIN;
        sr_delay();
        sio_hw->gpio_set = 1u << SR_SCK_PIN;
        sr_delay();
        sio_hw->gpio_clr = 1u << SR_SCK_PIN;
        bits <<= 1;
    }
    sio_hw->gpio_set = 1u << SR_RCK_PIN;
    sr_delay();
    sio_hw->gpio_clr = 1u << SR_RCK_PIN;
    fuji_cart_poke(FN_R_SRAM_STATE, (sr_shadow & SR_SRAM_EN) ? 1 : 0);
}

static void sr_update(uint8_t clear, uint8_t set)
{
    fuji_cart_sr_write((uint8_t)((sr_shadow & ~clear) | set));
}

void fuji_cart_set_irq(bool asserted)
{
    if (asserted) {
        gpio_put(IRQ_PIN, 0);
        gpio_set_dir(IRQ_PIN, true);
    } else {
        gpio_set_dir(IRQ_PIN, false);          /* the console's pull-up   */
    }
}

/* ---------------- the ring ---------------- */

/* Only an event at least two bus cycles old is handed out, so that the write
 * of the cycle after it -- an RMW's real write -- is already in the ring and
 * the dummy write can be recognised: same offset, adjacent cycle. */
bool fuji_cart_next_event(nes_event_t *ev)
{
    uint16_t tail = fuji_ring.tail;
    uint16_t head = fuji_ring.head;
    nes_event_t e;

    if (tail == head)
        return false;
    e.offset = fuji_ring.buf[tail].offset;
    e.data = fuji_ring.buf[tail].data;
    e.kind = fuji_ring.buf[tail].kind;
    e.cycle = fuji_ring.buf[tail].cycle;
    if ((int32_t)(fuji_bus_cycle - e.cycle) < 2 && console_alive)
        return false;
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);

    if (e.kind == NES_EV_MAILBOX && fuji_ring.tail != head) {
        uint16_t n = fuji_ring.tail;
        if (fuji_ring.buf[n].kind == NES_EV_MAILBOX
            && fuji_ring.buf[n].offset == e.offset
            && fuji_ring.buf[n].cycle == e.cycle + 1) {
            /* the dummy write of an RMW: drop it, count it, hand out the real one */
            diag_rmw++;
            fuji_cart_poke(FN_R_DIAG_RMW, diag_rmw);
            return fuji_cart_next_event(ev);
        }
    }
    *ev = e;
    return true;
}

/* Publish one mailbox byte into the arena. The loader ROM half is never
 * written this way. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (offset < FN_R_PAINT_END)
        fuji_arena[offset] = value;
}

/* ---------------- the mapper's slow outputs ---------------- */

static void apply_gates(void)
{
    uint8_t set = 0, clear = 0;
    const nesmap_out_t *o = &fuji_map.out;

    if (o->mirror & 1) set |= SR_MIR0; else clear |= SR_MIR0;
    if (o->mirror & 2) set |= SR_MIR1; else clear |= SR_MIR1;
    if (o->mirror == NESMAP_MIR_4) set |= SR_FOURSCREEN; else clear |= SR_FOURSCREEN;
    if (o->chr_wp) clear |= SR_CHR_WE_EN; else set |= SR_CHR_WE_EN;
    fuji_serve.wram_en = o->wram_en;
    fuji_serve.wram_wp = o->wram_wp;
    if (((sr_shadow & ~clear) | set) != sr_shadow)
        sr_update(clear, set);
}

static void apply_tables(void)
{
    unsigned i;

    for (i = 0; i < NESMAP_PRG_SLOTS; i++)
        nes_pio_patch_prg(i, fuji_map.out.prg[i]);
    for (i = 0; i < NESMAP_CHR_SLOTS; i++)
        nes_pio_patch_chr(i, fuji_map.out.chr[i]);
}

/* ---------------- staging ---------------- */

void fuji_cart_set_resident(const uint8_t *image, const nesmap_plan_t *plan)
{
    resident_base = image;
    resident_plan = *plan;
}

void fuji_cart_stage(const uint8_t *image, const nesmap_plan_t *plan)
{
    staged_base = image;
    staged_plan = *plan;
    fuji_have_staged = true;
}

bool fuji_cart_store_busy(const uint8_t *base)
{
    if (fuji_have_staged && base == staged_base)
        return true;
    if (ls_state != LS_IDLE && base == ls_base)
        return true;
    return false;
}

/* ---------------- the load sequence ---------------- */

static void publish_slice(void)
{
    const uint8_t *src;
    unsigned dst, off;

    if (ls_slice < ls_nprg) {
        unsigned bank8 = ls_slice / 8;
        nes_pio_patch_prg(0, bank8);           /* $8000-$9FFF -> this 8K   */
        src = ls_base + nesmap_prg_offset(&ls_plan) + ls_slice * 1024;
        dst = FN_LOAD_DST_PRG;
        off = ls_slice % 8;
    } else {
        unsigned k = (unsigned)(ls_slice - ls_nprg);
        unsigned w = k / 8, i;
        if (k == 0)                            /* first CHR slice: open the gate */
            sr_update(0, SR_CHR_WE_EN);
        for (i = 0; i < NESMAP_CHR_SLOTS; i++)
            nes_pio_patch_chr(i, w * 8 + i);   /* PPU $0000-$1FFF -> this 8K */
        src = ls_base + nesmap_chr_offset(&ls_plan) + k * 1024;
        dst = FN_LOAD_DST_CHR;
        off = k % 8;
    }
    memcpy(fuji_arena + FN_R_DATA, src, FN_R_SLICE_LEN);
    fuji_cart_poke(FN_R_LOAD_DST, (uint8_t)dst);
    fuji_cart_poke(FN_R_LOAD_OFF, (uint8_t)off);
    fuji_cart_poke(FN_R_LOAD_PCT, (uint8_t)((ls_slice * 100u) / (ls_nprg + ls_nchr)));
    ls_seq = (uint8_t)(ls_seq == 255 ? 1 : ls_seq + 1);
    __dmb();
    fuji_cart_poke(FN_R_LOAD_SEQ, ls_seq);     /* published LAST */
    fuji_cart_poke(FN_R_LOAD_STATE, FN_LOAD_SLICE);
    ls_ack_pending = true;
}

static void begin_load(const uint8_t *base, const nesmap_plan_t *plan)
{
    ls_base = base;
    ls_plan = *plan;
    ls_nprg = plan->prg_size / 1024;
    ls_nchr = plan->chr_size / 1024;
    ls_slice = 0;
    ls_ack_pending = false;
    ls_state = LS_RUN;
    fuji_have_staged = false;
    fuji_boot_armed = false;

    /* Order matters: core1 stops serving the vector page BEFORE the '595
     * enables the SRAM, so two drivers never meet on D0-D7. The loader
     * polls FN_R_SRAM_STATE, painted by the '595 write, before its first
     * SRAM store. */
    fuji_serve.loading = true;
    fuji_serve.mailbox = true;
    fuji_serve.sram_en = true;
    __dmb();
    fuji_cart_set_irq(false);
    nes_irq_enable(false);
    sr_update(SR_CHR_WE_EN, SR_SRAM_EN | SR_PRG_WE_EN);
    fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_IDLE);
    publish_slice();
}

static void finish_load(void)
{
    nesmap_init(&fuji_map, &ls_plan);
    apply_tables();
    apply_gates();
    sr_update(SR_PRG_WE_EN, SR_LED);
    fuji_serve.loading = false;
    nes_irq_enable(fuji_map.desc->irq == NESMAP_IRQ_A12);

    /* The mailbox stays live only for an image that claims it. It is not
     * switched off here: the loader still has to read FN_LOAD_DONE, and it
     * says goodbye with one more SLICE_ACK. */
    if (ls_plan.fuji_claim && !ls_plan.claims_5000)
        fujimail_paint();                      /* ACKSEQ restarts for the new client */
    fuji_cart_poke(FN_R_MAPPER, (uint8_t)ls_plan.mapper);
    fuji_cart_poke(FN_R_LOAD_STATE, FN_LOAD_DONE);
    ls_state = LS_DONE;
}

/* The loader's request. A client armed a staged image with BOOTLOCK, or --
 * at power-on and after a console power-cycle -- the loader was entered
 * through the cart-served vectors and the resident CONFIG is what it gets.
 * Nothing enables the SRAM before this request: the 6502 must have fetched
 * its reset vector from the cart first. */
void fuji_cart_request_load(void)
{
    if (ls_state != LS_IDLE)
        return;
    if (fuji_boot_armed && fuji_have_staged)
        begin_load(staged_base, &staged_plan);
    else if (ls_autoload && resident_base) {
        ls_autoload = false;
        begin_load(resident_base, &resident_plan);
    }
}

void fuji_cart_slice_acked(void)
{
    switch (ls_state) {
    case LS_RUN:
        if (!ls_ack_pending)
            return;
        ls_ack_pending = false;
        ls_slice++;
        if (ls_slice < ls_nprg + ls_nchr)
            publish_slice();
        else
            finish_load();
        break;
    case LS_DONE:
        /* The loader is about to JMP ($FFFC). */
        fuji_serve.mailbox = ls_plan.fuji_claim && !ls_plan.claims_5000;
        fuji_cart_poke(FN_R_LOAD_STATE, FN_LOAD_IDLE);
        ls_state = LS_IDLE;
        break;
    default:
        break;
    }
}

/* ---------------- console power ---------------- */

static void console_off(void)
{
    console_alive = false;
    ls_state = LS_IDLE;
    ls_ack_pending = false;
    fuji_serve.loading = false;
    fuji_serve.sram_en = false;
    fuji_serve.mailbox = true;
    __dmb();
    fuji_cart_set_irq(false);
    nes_irq_enable(false);
    sr_update(SR_SRAM_EN | SR_PRG_WE_EN | SR_CHR_WE_EN | SR_LED, 0);
    ls_autoload = true;                        /* CONFIG again when it comes back */
}

static void console_on(void)
{
    console_alive = true;                      /* the loader will ask */
}

/* ---------------- init and service ---------------- */

void fuji_cart_init(const uint8_t *loader_rom, unsigned loader_len)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    memset(fuji_arena, 0, sizeof fuji_arena);
    memset(fuji_wram, 0, sizeof fuji_wram);
    memset(fuji_vectors, 0, sizeof fuji_vectors);
    if (loader_len > FN_LOADER_SIZE)
        loader_len = FN_LOADER_SIZE;
    memcpy(fuji_arena + FN_LOADER, loader_rom, loader_len);

    /* The vector page the cart serves at power-on: RESET into the loader,
     * NMI and IRQ onto the RTI at its third byte (loader.s keeps them there). */
    fuji_vectors[0xFA] = 0x03; fuji_vectors[0xFB] = 0x58;
    fuji_vectors[0xFC] = 0x00; fuji_vectors[0xFD] = 0x58;
    fuji_vectors[0xFE] = 0x03; fuji_vectors[0xFF] = 0x58;

    fuji_serve.arena = fuji_arena;
    fuji_serve.wram = fuji_wram;
    fuji_serve.wram_size = NESMAP_WRAM_MAX;
    fuji_serve.vectors = fuji_vectors;
    fuji_serve.sram_en = false;
    fuji_serve.mailbox = true;
    fuji_serve.wram_en = false;
    fuji_serve.wram_wp = false;
    fuji_serve.loading = false;
    fuji_boot_armed = false;
    fuji_have_staged = false;
    fuji_slow_dirty = 0;
    fuji_irq_dirty = 0;
    ls_state = LS_IDLE;
    ls_autoload = true;
    console_alive = false;
    wd_last_cycle = 0;
    wd_last_change = get_absolute_time();
    fuji_cart_sr_write(0);
}

void fuji_cart_service(void)
{
    uint32_t cyc = fuji_bus_cycle;
    absolute_time_t now = get_absolute_time();

    /* The watchdog: M2 has stopped for 250 ms means the console is off. */
    if (cyc != wd_last_cycle) {
        wd_last_cycle = cyc;
        wd_last_change = now;
        if (!console_alive)
            console_on();
    } else if (console_alive
               && absolute_time_diff_us(wd_last_change, now) > 250000) {
        console_off();
    }

    if (fuji_slow_dirty) {
        fuji_slow_dirty = 0;                   /* clear first, then apply the latest */
        apply_gates();
    }
    if (fuji_irq_dirty) {
        fuji_irq_dirty = 0;
        fuji_cart_set_irq(fuji_map.irq_line);
    }
}
