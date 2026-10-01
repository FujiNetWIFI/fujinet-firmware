/* nesmap.h -- NES cartridge image mapping: the iNES/NES 2.0 header, and the
 * mapper engine that turns console writes into bank tables.
 *
 * Every mapper the EverDrive N8 supports reduces to the same outputs on this
 * hardware: four 8K PRG slots, eight 1K CHR slots, a mirroring mode, and the
 * WRAM/CHR write gates. The engine is a table of per-mapper handlers that all
 * produce one nesmap_out_t; three consumers read it and never disagree:
 *   - the cartridge firmware patches the PIO bank tables and the '595 from it
 *   - the MAME device indexes its PRG/CHR arrays through it
 *   - test_nesmap.c compares it against transcriptions of MAME's own handlers
 *
 * Hardware-free by design: compiled into the cartridge firmware, the MAME
 * device, and the host tests. Nothing here may touch pico-sdk.
 */

#ifndef NESMAP_H
#define NESMAP_H

#include <stdbool.h>
#include <stdint.h>

#define NESMAP_HDR_LEN   16u
#define NESMAP_PRG_MAX   (512u * 1024)          /* the PRG SRAM              */
#define NESMAP_CHR_MAX   (512u * 1024)          /* the CHR SRAM              */
#define NESMAP_WRAM_MAX  (32u * 1024)           /* cart-served PRG-RAM       */
#define NESMAP_IMAGE_MAX (NESMAP_HDR_LEN + NESMAP_PRG_MAX + NESMAP_CHR_MAX)

#define NESMAP_PRG_SLOTS 4                      /* 8K each, $8000/$A000/$C000/$E000 */
#define NESMAP_CHR_SLOTS 8                      /* 1K each, $0000..$1C00     */

/* Mirroring codes double as the 74HCT253 select value: what CIRAM A10 follows. */
enum {
    NESMAP_MIR_V   = 0,   /* CIRAM A10 = PPU A10: vertical mirroring  */
    NESMAP_MIR_H   = 1,   /* CIRAM A10 = PPU A11: horizontal          */
    NESMAP_MIR_1LO = 2,   /* CIRAM A10 = 0: one-screen, lower bank    */
    NESMAP_MIR_1HI = 3,   /* CIRAM A10 = 1: one-screen, upper bank    */
    NESMAP_MIR_4   = 4,   /* four-screen: CIRAM /CE off (deferred)    */
};

typedef enum {
    NESMAP_OK = 0,
    NESMAP_EEMPTY,     /* fewer than 16 bytes                          */
    NESMAP_EMAGIC,     /* no "NES\x1A"                                 */
    NESMAP_ETRAINER,   /* 512-byte trainer: refused, never used by anything worth booting */
    NESMAP_ETOOBIG,    /* PRG or CHR over what the SRAMs hold          */
    NESMAP_ETRUNC,     /* image shorter than the header promises       */
    NESMAP_EMAPPER,    /* mapper not in the table                      */
} nesmap_err_t;

typedef struct {
    uint16_t mapper;
    uint8_t  submapper;
    uint32_t prg_size;       /* bytes of PRG-ROM in the image            */
    uint32_t chr_size;       /* bytes of CHR-ROM in the image, 0 = CHR-RAM */
    uint32_t chr_ram_size;   /* CHR-RAM the board has, 0 if CHR-ROM      */
    uint32_t prg_ram_size;   /* WRAM the cart serves at $6000, 0 = none  */
    uint32_t image_size;     /* 16 + prg + chr: what a whole push must be */
    bool     battery;
    bool     four_screen;    /* header flag; also "mapper controls one-screen" on 30/71 */
    uint8_t  mirror;         /* NESMAP_MIR_H or _V from the header (or _4) */
    bool     claims_5000;    /* the board decodes $5000-$5FFF: mailbox off */
    bool     fuji_claim;     /* "FUJI" at prg_size-16: keep the mailbox    */
} nesmap_plan_t;

typedef struct {
    uint8_t  prg[NESMAP_PRG_SLOTS];   /* 8K bank number per CPU slot        */
    uint16_t chr[NESMAP_CHR_SLOTS];   /* 1K bank number per PPU slot        */
    uint8_t  mirror;                  /* NESMAP_MIR_*                       */
    bool     wram_en;                 /* $6000-$7FFF answers                */
    bool     wram_wp;                 /* ...but refuses writes              */
    bool     chr_wp;                  /* PPU writes to CHR are dropped      */
} nesmap_out_t;

struct nesmap_desc;

typedef struct nesmap {
    nesmap_plan_t plan;
    const struct nesmap_desc *desc;
    uint32_t prg8;                /* 8K PRG banks in the image            */
    uint32_t chr1;                /* 1K CHR banks (ROM, or the RAM size)  */
    uint8_t  reg[8];              /* generic mapper registers             */
    uint8_t  shift, shift_n;      /* MMC1 serial port                     */
    uint32_t last_write_cycle;    /* MMC1: the cycle of the last $8000+ write */
    bool     consecutive;         /* ...and whether this write is the next cycle */
    uint8_t  bank_sel;            /* MMC3 / Namcot 108 $8000 latch        */
    uint8_t  bank[8];             /* MMC3 / Namcot 108 R0-R7              */
    uint8_t  irq_latch, irq_counter;
    bool     irq_reload, irq_enable, irq_line;
    nesmap_out_t out;
    uint8_t  dirty;               /* NESMAP_DIRTY_*                       */
} nesmap_t;

#define NESMAP_DIRTY_PRG  0x01
#define NESMAP_DIRTY_CHR  0x02
#define NESMAP_DIRTY_SLOW 0x04    /* mirror, wram gates, chr_wp           */
#define NESMAP_DIRTY_IRQ  0x08    /* irq_line changed by a write          */

typedef enum {
    NESMAP_IRQ_NONE = 0,
    NESMAP_IRQ_A12,               /* MMC3-style scanline counter          */
} nesmap_irq_t;

typedef struct nesmap_desc {
    uint16_t mapper;
    const char *name;
    void (*reset)(nesmap_t *m);
    void (*write)(nesmap_t *m, uint16_t addr, uint8_t data);
    uint16_t write_lo;            /* lowest CPU address the handler sees  */
    bool claims_5000;
    uint8_t irq;                  /* nesmap_irq_t                         */
} nesmap_desc_t;

/* Size gate at stream-open time, before the ESP32 drags the file over the
 * network; 0 to accept (size 0 = still unknown) or an FN_BOOT_ERR_*. */
uint8_t nesmap_gate(uint32_t size);

/* Parse the header and locate the claim. `size` is the whole file. */
nesmap_err_t nesmap_plan(const uint8_t *image, uint32_t size, nesmap_plan_t *plan);

const nesmap_desc_t *nesmap_find(uint16_t mapper);

/* Bind a plan to its handler and put the mapper in its power-on state. */
nesmap_err_t nesmap_init(nesmap_t *m, const nesmap_plan_t *plan);
void nesmap_reset(nesmap_t *m);

/* One console write. `cycle` counts M2 rising edges; it exists for MMC1's
 * consecutive-write rule, which needs the cart's clock rather than the
 * emulator's. Sets m->dirty for whatever changed. */
void nesmap_write(nesmap_t *m, uint16_t addr, uint8_t data, uint32_t cycle);

/* MMC3-style scanline counter: one filtered PPU A12 rising edge. Returns the
 * new state of the IRQ line. */
bool nesmap_a12_clock(nesmap_t *m);

/* The image offsets the loader copies from: PRG starts right after the header,
 * CHR right after PRG. */
static inline uint32_t nesmap_prg_offset(const nesmap_plan_t *p)
{
    (void)p;
    return NESMAP_HDR_LEN;
}
static inline uint32_t nesmap_chr_offset(const nesmap_plan_t *p)
{
    return NESMAP_HDR_LEN + p->prg_size;
}

#endif /* NESMAP_H */
