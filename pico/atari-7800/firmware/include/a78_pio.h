/* a78_pio.h -- the PIO slot table, as instruction words.
 *
 * The SRAM's high address bits and the glue's per-slot enables come from one
 * PIO state machine that is nothing but a lookup table: a 4-instruction loop
 * turns CPU A13-A15 into a slot address, and each 2-instruction slot is
 * `set pins, lo5 side hi4` + `jmp loop`. The 9 bits are an a78map slot word
 * (A13-A18, ROM_EN, RAM_EN, A8MASK). A change is one 32-bit store into the
 * SM's instruction memory, a "1-write, 4-read register file": the SM reads
 * the old word or the new one, never a torn one.
 *
 * Latency, from an address change to the pins: at most 9 instructions (A13-A15
 * bypass the input synchroniser), about 45 ns at 200 MHz. With the SRAM's
 * 55 ns that is about 115 ns, against about 200 ns usable in a MARIA
 * display-list read.
 *
 * Hardware-free: the encoders are the SDK's pio_encode_* by hand, so the host
 * tests build the exact words the firmware patches, and a78_pio.c checks them
 * against pioasm's output of a78_tables.pio at init.
 */

#ifndef A78_PIO_H
#define A78_PIO_H

#include <stdint.h>

#define A78_PIO_SLOTS      8      /* 8K each, indexed by CPU A13-A15        */
#define A78_PIO_INDEX_BITS 3
#define A78_PIO_SIDE_BITS  4      /* 9 = 5 (set) + 4 (side-set)             */
#define A78_PIO_LOOP_LEN   4
#define A78_PIO_PROG_LEN   (2 * A78_PIO_SLOTS + A78_PIO_LOOP_LEN)   /* 20 */

/* Instruction encodings (RP2350 datasheet 11.4). No delay bits are ever used;
 * with `.side_set N opt` bit 12 is the side-set enable and the N bits below
 * it are the side value. */
static inline uint16_t a78_pio_enc_jmp(unsigned addr)
{
    return (uint16_t)(0x0000u | (addr & 0x1Fu));
}
static inline uint16_t a78_pio_enc_in(unsigned src, unsigned count)
{
    return (uint16_t)(0x4000u | ((src & 7u) << 5) | (count & 0x1Fu));
}
static inline uint16_t a78_pio_enc_mov(unsigned dest, unsigned src)
{
    return (uint16_t)(0xA000u | ((dest & 7u) << 5) | (src & 7u));
}
static inline uint16_t a78_pio_enc_set_pins(unsigned v5)
{
    return (uint16_t)(0xE000u | (v5 & 0x1Fu));
}
static inline uint16_t a78_pio_enc_sideset_opt(unsigned nbits, unsigned v)
{
    return (uint16_t)(0x1000u | ((v & ((1u << nbits) - 1u)) << (12u - nbits)));
}

#define A78_PIO_SRC_PINS 0u
#define A78_PIO_SRC_NULL 3u
#define A78_PIO_SRC_ISR  6u
#define A78_PIO_DST_PC   5u
#define A78_PIO_DST_ISR  6u

/* One slot's first instruction, carrying the whole 9-bit slot word. */
static inline uint16_t a78_pio_slot_instr(unsigned word)
{
    return (uint16_t)(a78_pio_enc_set_pins(word & 0x1Fu)
                      | a78_pio_enc_sideset_opt(A78_PIO_SIDE_BITS, word >> 5));
}

/* The slot word an instruction carries; the inverse, for the tests. */
static inline unsigned a78_pio_slot_word(uint16_t instr)
{
    return (instr & 0x1Fu) | (((instr >> (12u - A78_PIO_SIDE_BITS)) & 0xFu) << 5);
}

/* The whole program: slots at 0,2,..; the loop after them. */
static inline unsigned a78_pio_build(uint16_t *prog, const uint16_t *words)
{
    unsigned loop = 2 * A78_PIO_SLOTS, i;

    for (i = 0; i < A78_PIO_SLOTS; i++) {
        prog[2 * i] = a78_pio_slot_instr(words ? words[i] : 0);
        prog[2 * i + 1] = a78_pio_enc_jmp(loop);
    }
    prog[loop + 0] = a78_pio_enc_mov(A78_PIO_DST_ISR, A78_PIO_SRC_NULL);
    prog[loop + 1] = a78_pio_enc_in(A78_PIO_SRC_PINS, A78_PIO_INDEX_BITS);
    prog[loop + 2] = a78_pio_enc_in(A78_PIO_SRC_NULL, 1);
    prog[loop + 3] = a78_pio_enc_mov(A78_PIO_DST_PC, A78_PIO_SRC_ISR);
    return loop + A78_PIO_LOOP_LEN;
}

#ifndef A78_PIO_ON_HOST
/* Firmware-only API (a78_pio.c). */
void a78_pio_init(void);
void a78_pio_patch(unsigned slot, unsigned word);
void a78_pio_load(const uint16_t *words);     /* all eight slots */
#endif

#endif /* A78_PIO_H */
