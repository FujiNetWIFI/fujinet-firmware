/* stub/hardware/flash.h -- the flash HAL, modelled rather than faked.
 *
 * A program can only clear bits: it ANDs into the part, exactly as NOR flash
 * does. Skipping an erase therefore corrupts the image instead of silently
 * working, which is the whole point of testing the erase bookkeeping.
 */
#ifndef STUB_HARDWARE_FLASH_H
#define STUB_HARDWARE_FLASH_H

#include "pico/stdlib.h"

#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE    256u

extern unsigned fake_flash_erases;
extern unsigned fake_flash_programs;

void flash_range_erase(uint32_t off, size_t len);
void flash_range_program(uint32_t off, const uint8_t *src, size_t len);

#endif /* STUB_HARDWARE_FLASH_H */
