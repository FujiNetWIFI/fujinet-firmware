/* stub/pico/stdlib.h -- what fuji_store.c and fuji_watchdog.c need from the SDK on the
 * host; the test defines what is declared here.
 */
#ifndef STUB_PICO_STDLIB_H
#define STUB_PICO_STDLIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICO_FLASH_SIZE_BYTES (2u * 1024 * 1024)

/* The flash "part" is this array, so XIP addresses point into it. */
extern uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)fake_flash)

uint64_t time_us_64(void);

#endif /* STUB_PICO_STDLIB_H */
