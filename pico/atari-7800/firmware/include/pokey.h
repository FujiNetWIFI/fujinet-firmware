/* pokey.h -- the cart's stand-in for a POKEY: registers and sound.
 *
 * Ballblazer and Commando carry a POKEY at $4000; FujiNet apps get one at
 * $0450. core1 answers reads from a 16-byte table this keeps current and
 * passes writes through the ring; core0 renders the sound into a PWM.
 *
 * The sound is event-driven: each channel's next divider underflow is
 * computed rather than stepping every 1.79 MHz clock, and the polynomial
 * counters are tables indexed by machine clocks since SKCTL last released
 * them, as the real chip's run free once out of reset.
 *
 * Hardware-free, so the host tests drive it.
 */

#ifndef POKEY_H
#define POKEY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    POKEY_AUDF1 = 0, POKEY_AUDC1, POKEY_AUDF2, POKEY_AUDC2,
    POKEY_AUDF3, POKEY_AUDC3, POKEY_AUDF4, POKEY_AUDC4,
    POKEY_AUDCTL, POKEY_STIMER, POKEY_SKRES, POKEY_POTGO,
    POKEY_SEROUT = 0x0D, POKEY_IRQEN, POKEY_SKCTL,
};
enum {
    POKEY_ALLPOT = 0x08, POKEY_KBCODE, POKEY_RANDOM,
    POKEY_SERIN = 0x0D, POKEY_IRQST, POKEY_SKSTAT,
};

#define POKEY_POLY4_LEN  15u
#define POKEY_POLY5_LEN  31u
#define POKEY_POLY9_LEN  511u
#define POKEY_POLY17_LEN 131071u

typedef struct {
    uint8_t audf[4], audc[4];
    uint8_t audctl, skctl;
    uint64_t t;                   /* machine clocks rendered so far          */
    uint64_t next[4];             /* clock of each channel's next underflow  */
    uint8_t out[4];               /* each channel's flip-flop                */
    uint8_t hp[2];                /* the high-pass latches for channels 1, 2 */
    uint64_t poly_t0;             /* when SKCTL last released the polys      */
} pokey_t;

void pokey_init(pokey_t *p);

/* A register write that happened at machine clock `t` (>= p->t). Renders up
 * to it first is the caller's job if it cares about sub-sample timing. */
void pokey_write(pokey_t *p, unsigned reg, uint8_t v, uint64_t t);

/* What a read of each register returns at machine clock `t`. */
uint8_t pokey_read(const pokey_t *p, unsigned reg, uint64_t t);

/* Render `n` samples, each `clocks_per_sample` machine clocks long; samples
 * are the sum of the four channels' volumes, 0-60. */
void pokey_render(pokey_t *p, uint8_t *out, unsigned n, unsigned clocks_per_sample);

/* The polynomial counters' output bit at step `i` (tables built once). */
bool pokey_poly4(uint32_t i);
bool pokey_poly5(uint32_t i);
bool pokey_poly9(uint32_t i);
bool pokey_poly17(uint32_t i);

#endif /* POKEY_H */
