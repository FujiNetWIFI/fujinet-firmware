/* nes_irq.h -- the MMC3 scanline counter: a PIO2 state machine filters PPU
 * A12 rises the way the MMC3 does, and its interrupt clocks nesmap. */
#ifndef NES_IRQ_H
#define NES_IRQ_H

#include <stdbool.h>

/* The MMC3 counts an A12 rise only after A12 has been low for three M2
 * falling edges (nesdev); the filter polls for this long instead. */
#ifndef MMC3_A12_FILTER_NS
#define MMC3_A12_FILTER_NS 1700
#endif

void nes_irq_init(void);
void nes_irq_enable(bool on);   /* only while the live mapper has one */

#endif /* NES_IRQ_H */
