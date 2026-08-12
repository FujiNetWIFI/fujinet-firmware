#ifndef INTELLIVOICE_MINTY_H
#define INTELLIVOICE_MINTY_H

#include <stdint.h>
#include <stdbool.h>
#include "ivoice.h"

/*
 * Minty Intellivoice / SP0256 wrapper.
 *
 * Bus mapping, as used by the Intellivoice:
 *   $0080 : SP0256 ALD command write / LRQ status read
 *   $0081 : SPB-640 FIFO write / FIFO full status read
 *
 * The low-level SP0256 emulation is provided by ivoice.c / ivoice.h.
 */

#define IVOICE_ADDR_ALD   0x0080
#define IVOICE_ADDR_FIFO  0x0081

/* tv_mode follows existing Minty/ECS convention: 0 = PAL, non-zero = NTSC. */
void init_intellivoice(uint8_t tv_mode);
void intellivoice_reset(void);


/*
 * Called once per ECS audio timer tick.
 * Advances SP0256 timing, applies pending bus writes, and returns the next
 */
int16_t intellivoice_next_sample(void);

#endif /* INTELLIVOICE_MINTY_H */
