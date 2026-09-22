/* stub/pico/stdlib.h -- enough of the SDK for the desktop build of
 * fuji_store.c. The real header is a platform grab-bag; this is only what
 * that file actually needs, plus the flash geometry it asserts against.
 */
#ifndef STUB_PICO_STDLIB_H
#define STUB_PICO_STDLIB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICO_FLASH_SIZE_BYTES (2u * 1024 * 1024)

/* The RP2040 maps flash into the address space at XIP_BASE; here the "part"
 * is an array, so the store's XIP alias is a pointer into it. */
extern uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)fake_flash)

#endif /* STUB_PICO_STDLIB_H */
