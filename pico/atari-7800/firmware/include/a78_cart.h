/* a78_cart.h -- the 7800 cartridge bus as seen from the RP2354B.
 *
 * The console addresses a 512K SRAM itself (A0-A12 direct); a PIO table
 * indexed by A13-A15 supplies the page, and whether the SRAM may answer or be
 * written, for each 8K slot. 74HCT gates decide who drives D0-D7 from the
 * address and those slot bits; software is never in a game's data path,
 * because MARIA's DMA reads the cart too, in 280-420 ns windows.
 *
 * core1 serves only what the cart keeps in its own memory -- the boot block,
 * the mailbox arena, POKEY registers -- and watches every CPU write. The
 * decode is static inline here so core1, the MAME device and the host tests
 * share one implementation.
 *
 * Timing (not to be re-derived): the 6502C runs at 1.79 MHz, 559 ns a cycle;
 * the address is stable while PHI2 is high and write data is valid at its
 * falling edge. PROVISIONAL until a scope says otherwise: how long write
 * data holds after PHI2 falls, hence the last-sample-while-high model.
 */
#ifndef A78_CART_H
#define A78_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "fuji_mailbox.h"
#include "a78map.h"

/* --------------------------------------------------------------------------
 * GPIO map (RP2354B). GPIO0-31 are the inputs core1 reads in one word, all
 * 5 V-tolerant; GPIO40-47 are not, so they only drive 5 V CMOS inputs.
 * -------------------------------------------------------------------------- */
#define D0_PIN       0u     /* D0-D7, bidirectional, 100 R series          */
#define A0_PIN       8u     /* A0-A15; A13-A15 (GPIO21-23) index the PIO table */
#define RW_PIN       24u    /* 1 = the CPU reads                           */
#define PHI2_PIN     25u    /* PWM 4 B: counts CPU cycles                  */
#define HALT_PIN     26u    /* MARIA owns the bus while low; observed only */
#define PWROK_PIN    27u    /* console +5V present (74HCT14)               */
#define IRQ_PIN      28u    /* 2N7002 gate: high pulls /IRQ low; never set */
#define AUDIO_PIN    29u    /* PWM 6 B -> RC -> edge pin 18                */
#define LED_PIN      30u
#define SLOT_PIN     32u    /* PIO table out: SRAM A13-A18, ROM_EN, RAM_EN, A8MASK */
#define DBG_TX_PIN   44u
#define DBG_RX_PIN   45u

#define ADDR_SHIFT   A0_PIN
#define DATA_MASK    (0xFFu << D0_PIN)
#define RW_MASK      (1u << RW_PIN)
#define PHI2_MASK    (1u << PHI2_PIN)
#define HALT_MASK    (1u << HALT_PIN)
#define PWROK_MASK   (1u << PWROK_PIN)

static inline uint16_t a78_addr(uint32_t pins) { return (uint16_t)(pins >> ADDR_SHIFT); }
static inline uint8_t a78_data(uint32_t pins) { return (uint8_t)(pins >> D0_PIN); }

/* --------------------------------------------------------------------------
 * The glue (74HCT, Rev0). The SRAM may only ever answer in the cart's own
 * ranges: $4000-$FFFF, and the HSC's $1000-$17FF and $3000-$3FFF. /OE does
 * not use PHI2 -- MARIA's reads are not PHI2-aligned -- but /WE does.
 * -------------------------------------------------------------------------- */
static inline bool a78_cartsel(uint16_t a)
{
    return (a & 0xC000) != 0 || (a & 0xF800) == 0x1000 || (a & 0xF000) == 0x3000;
}

static inline bool a78_glue_oe(bool pwr_ok, uint16_t slot, uint16_t a, bool rw)
{
    return pwr_ok && rw && (slot & A78S_ROM_EN) && a78_cartsel(a);
}

static inline bool a78_glue_we(bool pwr_ok, uint16_t slot, uint16_t a, bool rw, bool phi2)
{
    return pwr_ok && !rw && phi2 && (slot & A78S_RAM_EN) && a78_cartsel(a);
}

static inline bool a78_glue_a8(uint16_t slot, uint16_t a)
{
    return (a & 0x100) && !(slot & A78S_A8MASK);
}

/* --------------------------------------------------------------------------
 * What the cart serves and what it makes of a write.
 * -------------------------------------------------------------------------- */
static inline bool a78_in_loader(uint16_t a)
{
    return a >= FN_LOADER_BASE && a < FN_LOADER_BASE + FN_LOADER_SIZE;
}
static inline bool a78_in_arena(uint16_t a)
{
    return a >= FN_ARENA_BASE && a < FN_ARENA_BASE + FN_ARENA_SIZE;
}
static inline bool a78_in_pokey450(uint16_t a) { return (a & 0xFFF0) == 0x0450; }
static inline bool a78_in_hscram(uint16_t a) { return (a & 0xF800) == 0x1000; }

/* TIA and its mirrors: where an unlocked INPTCTRL takes every write. */
static inline bool a78_in_inptctrl(uint16_t a) { return (a & 0xFCE0) == 0x0000; }

typedef struct {
    uint8_t mode;             /* FN_MODE_*                                   */
    bool mbox;                /* the loader and the arena answer             */
    bool bootblk;             /* the boot block answers at $F000-$FFFF       */
    uint8_t pokey;            /* A78_POKEY_* bits of what is being served    */
    bool hsc;                 /* HSC RAM writes are shadowed                 */
    bool go_bios;             /* flip when INPTCTRL next maps the BIOS in    */
    uint8_t inptctrl;         /* the console's register, as far as we know   */
    bool inpt_locked;
} a78_bus_t;

typedef enum {
    A78_R_NONE = 0,           /* not ours: console, the SRAM, or nobody      */
    A78_R_BOOTBLK,            /* offset into the boot block                  */
    A78_R_LOADER,             /* offset into the loader                      */
    A78_R_ARENA,              /* arena offset (painted part only)            */
    A78_R_POKEY,              /* POKEY register 0-15                         */
} a78_read_t;

/* What core1 answers a read with; *off is the index into that memory. */
static inline a78_read_t a78_read_kind(const volatile a78_bus_t *b, uint16_t a, unsigned *off)
{
    if (a >= FN_BOOTBLK_BASE) {
        if (b->bootblk) {
            *off = a - FN_BOOTBLK_BASE;
            return A78_R_BOOTBLK;
        }
        return A78_R_NONE;
    }
    if (a < 0x1000) {
        if (b->mbox && a78_in_loader(a)) {
            *off = a - FN_LOADER_BASE;
            return A78_R_LOADER;
        }
        if (b->mbox && a78_in_arena(a)) {
            *off = a - FN_ARENA_BASE;
            return *off < FN_R_PAINT_END ? A78_R_ARENA : A78_R_NONE;
        }
        if ((b->pokey & A78_POKEY_0450) && a78_in_pokey450(a)) {
            *off = a & 0x0F;
            return A78_R_POKEY;
        }
        return A78_R_NONE;
    }
    if ((b->pokey & A78_POKEY_4000) && (a & 0xC000) == 0x4000) {
        *off = a & 0x0F;
        return A78_R_POKEY;
    }
    return A78_R_NONE;
}

enum {
    A78_W_NONE = 0,
    A78_W_MAILBOX,            /* a hotspot: arena offset for fujimail, via the ring */
    A78_W_MAPPER,             /* a78map_write, inline                       */
    A78_W_POKEY,              /* a POKEY register, via the ring             */
    A78_W_HSC,                /* HSC RAM: into the shadow, inline           */
    A78_W_INPTCTRL,           /* a TIA-range write: the INPTCTRL model      */
    A78_W_SWAP,               /* FN_HOT_SWAP: into LOAD, the armed image    */
    A78_W_CONFIG,             /* FN_HOT_CONFIG: into LOAD, CONFIG           */
    A78_W_GO,                 /* FN_HOT_GO: the loaded image, now           */
    A78_W_GO_BIOS,            /* FN_HOT_GO_BIOS: the loaded image, when the BIOS maps in */
};

static inline int a78_write_kind(const volatile a78_bus_t *b, uint16_t a)
{
    if (a < 0x1000) {
        if (a78_in_inptctrl(a))
            return A78_W_INPTCTRL;
        if (b->mbox && a78_in_arena(a)) {
            unsigned off = a - FN_ARENA_BASE;
            unsigned page = off & FN_H_PAGE_MASK;

            if (page == FN_H_REGSEL) {
                switch (off & 0xFF) {
                case FN_HOT_SWAP:    return A78_W_SWAP;
                case FN_HOT_CONFIG:  return A78_W_CONFIG;
                case FN_HOT_GO:      return A78_W_GO;
                case FN_HOT_GO_BIOS: return A78_W_GO_BIOS;
                default:             return A78_W_MAILBOX;
                }
            }
            if (page == FN_H_REGDATA || page == FN_H_DATA)
                return A78_W_MAILBOX;
            return A78_W_NONE;
        }
        if ((b->pokey & A78_POKEY_0450) && a78_in_pokey450(a))
            return A78_W_POKEY;
        return A78_W_NONE;
    }
    if (a < 0x4000)
        return b->hsc && a78_in_hscram(a) ? A78_W_HSC : A78_W_NONE;
    if ((b->pokey & A78_POKEY_4000) && (a & 0xC000) == 0x4000)
        return A78_W_POKEY;
    if (b->mode == FN_MODE_GAME || b->mode == FN_MODE_APP)
        return A78_W_MAPPER;
    return A78_W_NONE;
}

/* A TIA-range write, as INPTCTRL sees it. Returns true if it is the armed
 * GO_BIOS flip: the write that maps the BIOS back in. */
static inline bool a78_inptctrl_write(a78_bus_t *b, uint8_t d)
{
    if (!b->inpt_locked) {
        b->inptctrl = d;
        if (d & 0x01)
            b->inpt_locked = true;
    }
    if (b->go_bios && !(d & 0x04)) {
        b->go_bios = false;
        return true;
    }
    return false;
}

/* Power-on: the console is about to run its BIOS, which reads the boot block. */
static inline void a78_bus_reset(a78_bus_t *b)
{
    b->mode = FN_MODE_BOOT;
    b->mbox = true;
    b->bootblk = true;
    b->pokey = A78_POKEY_NONE;
    b->hsc = false;
    b->go_bios = false;
    b->inptctrl = 0;
    b->inpt_locked = false;
}

/* The LOAD mode the loader runs in: the loader, the arena and the slot-2 window. */
static inline void a78_bus_load(a78_bus_t *b)
{
    b->mode = FN_MODE_LOAD;
    b->mbox = true;
    b->bootblk = false;
    b->pokey = A78_POKEY_NONE;
    b->hsc = false;
    b->go_bios = false;
}

/* Into the loaded image. */
static inline void a78_bus_run(a78_bus_t *b, uint8_t mode, uint8_t pokey, bool hsc)
{
    b->mode = mode;
    b->mbox = mode == FN_MODE_APP;
    b->bootblk = false;
    b->pokey = pokey;
    b->hsc = hsc;
    b->go_bios = false;
}

/* core1 -> core0 ring entry, tagged with the bus cycle so the drain can spot
 * an RMW's dummy write (same address, adjacent cycle). */
typedef struct {
    uint16_t offset;          /* arena offset, or POKEY register            */
    uint8_t  data;
    uint8_t  kind;            /* A78_W_*                                    */
    uint32_t cycle;
} a78_event_t;

/* An RMW instruction writes the old value, then the new one, to the same
 * address on adjacent cycles; no plain store can. True if `first` is that
 * dummy write and `second` the real one. */
static inline bool a78_rmw_dummy(const a78_event_t *first, const a78_event_t *second)
{
    return first->kind == A78_W_MAILBOX && second->kind == A78_W_MAILBOX
        && first->offset == second->offset && second->cycle == first->cycle + 1;
}

void a78_core1_main(void);

#endif /* A78_CART_H */
