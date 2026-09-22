/* stub/hardware/sync.h -- the interrupt-disable pair, a no-op on the host. */
#ifndef STUB_HARDWARE_SYNC_H
#define STUB_HARDWARE_SYNC_H

#include "pico/stdlib.h"

static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t state) { (void)state; }

#endif /* STUB_HARDWARE_SYNC_H */
