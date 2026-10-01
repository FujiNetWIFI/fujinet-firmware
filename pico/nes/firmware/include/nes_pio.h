/* nes_pio.h -- the PIO bank tables, as instruction words.
 *
 * The PRG and CHR SRAMs get their high address bits from two PIO state
 * machines that are nothing but lookup tables: a 4-instruction loop turns
 * PPU A10-A12 (or CPU A13-A14) into a slot address, and each 2-instruction
 * slot is `set pins, lo5 [side hi]` + `jmp loop`. A bank change is one 32-bit
 * store into the SM's instruction memory, which the datasheet describes as a
 * "1-write, 4-read register file": the SM reads the old word or the new one,
 * never a torn one, and with optional side-set every bank bit rides in that
 * one word.
 *
 * Latency, from a PPU address change to the bank pins: the 2-cycle input
 * synchroniser plus at most 9 instructions (the SM had just sampled), 45 ns
 * at 250 MHz, against a 372 ns PPU access. The CPU side has the same figure
 * against 350 ns.
 *
 * This header is hardware-free: the encoders below are the SDK's
 * pio_encode_* by hand, so the host tests can build the exact words the
 * firmware patches, and nes_pio.c checks them against pioasm's output of
 * nes_tables.pio at init.
 */

#ifndef NES_PIO_H
#define NES_PIO_H

#include <stdint.h>

#define NES_PIO_CHR_SLOTS      8      /* 1K each, indexed by PPU A10-A12   */
#define NES_PIO_CHR_INDEX_BITS 3
#define NES_PIO_CHR_BANK_BITS  9      /* CHR SRAM A10-A18: 512K            */
#define NES_PIO_CHR_SIDE_BITS  4      /* 9 = 5 (set) + 4 (side-set)        */

#define NES_PIO_PRG_SLOTS      4      /* 8K each, indexed by CPU A13-A14   */
#define NES_PIO_PRG_INDEX_BITS 2
#define NES_PIO_PRG_BANK_BITS  6      /* PRG SRAM A13-A18: 512K            */
#define NES_PIO_PRG_SIDE_BITS  1      /* 6 = 5 (set) + 1 (side-set)        */

#define NES_PIO_LOOP_LEN       4
#define NES_PIO_CHR_PROG_LEN   (2 * NES_PIO_CHR_SLOTS + NES_PIO_LOOP_LEN)   /* 20 */
#define NES_PIO_PRG_PROG_LEN   (2 * NES_PIO_PRG_SLOTS + NES_PIO_LOOP_LEN)   /* 12 */

/* Instruction encodings (RP2350 datasheet 11.4). No delay bits are ever used;
 * with `.side_set N opt` bit 12 is the side-set enable and the N bits below
 * it are the side value. */
static inline uint16_t nes_pio_enc_jmp(unsigned addr)
{
    return (uint16_t)(0x0000u | (addr & 0x1Fu));           /* jmp always */
}
static inline uint16_t nes_pio_enc_in(unsigned src, unsigned count)
{
    return (uint16_t)(0x4000u | ((src & 7u) << 5) | (count & 0x1Fu));
}
static inline uint16_t nes_pio_enc_mov(unsigned dest, unsigned src)
{
    return (uint16_t)(0xA000u | ((dest & 7u) << 5) | (src & 7u));
}
static inline uint16_t nes_pio_enc_set_pins(unsigned v5)
{
    return (uint16_t)(0xE000u | (v5 & 0x1Fu));             /* dest = PINS */
}
static inline uint16_t nes_pio_enc_sideset_opt(unsigned nbits, unsigned v)
{
    return (uint16_t)(0x1000u | ((v & ((1u << nbits) - 1u)) << (12u - nbits)));
}

#define NES_PIO_SRC_PINS 0u
#define NES_PIO_SRC_NULL 3u
#define NES_PIO_SRC_ISR  6u
#define NES_PIO_DST_PC   5u
#define NES_PIO_DST_ISR  6u

/* One slot's first instruction, carrying the whole bank number. */
static inline uint16_t nes_pio_chr_slot_instr(unsigned bank)
{
    return (uint16_t)(nes_pio_enc_set_pins(bank & 0x1Fu)
                      | nes_pio_enc_sideset_opt(NES_PIO_CHR_SIDE_BITS, bank >> 5));
}
static inline uint16_t nes_pio_prg_slot_instr(unsigned bank)
{
    return (uint16_t)(nes_pio_enc_set_pins(bank & 0x1Fu)
                      | nes_pio_enc_sideset_opt(NES_PIO_PRG_SIDE_BITS, bank >> 5));
}

/* Build a whole table program: slots at 0,2,..; the loop after them.
 * `banks` may be NULL for all-zero. Returns the program length. */
static inline unsigned nes_pio_build_table(uint16_t *prog, unsigned nslots,
                                           unsigned index_bits, unsigned side_bits,
                                           const unsigned *banks)
{
    unsigned loop = 2 * nslots, i;

    for (i = 0; i < nslots; i++) {
        unsigned bank = banks ? banks[i] : 0;
        prog[2 * i] = (uint16_t)(nes_pio_enc_set_pins(bank & 0x1Fu)
                                 | nes_pio_enc_sideset_opt(side_bits, bank >> 5));
        prog[2 * i + 1] = nes_pio_enc_jmp(loop);
    }
    prog[loop + 0] = nes_pio_enc_mov(NES_PIO_DST_ISR, NES_PIO_SRC_NULL);
    prog[loop + 1] = nes_pio_enc_in(NES_PIO_SRC_PINS, index_bits);
    prog[loop + 2] = nes_pio_enc_in(NES_PIO_SRC_NULL, 1);
    prog[loop + 3] = nes_pio_enc_mov(NES_PIO_DST_PC, NES_PIO_SRC_ISR);
    return loop + NES_PIO_LOOP_LEN;
}

/* MMC3 A12 filter: `set x, N` iterations of two 16-cycle instructions. */
#define NES_PIO_A12_ITER_CYCLES 32u

#ifndef NES_PIO_ON_HOST
/* Firmware-only API (nes_pio.c). */
void nes_pio_init(void);
void nes_pio_patch_prg(unsigned slot, unsigned bank);
void nes_pio_patch_chr(unsigned slot, unsigned bank);
#endif

#endif /* NES_PIO_H */
