/* stub/hardware/flash.h -- the flash HAL as NOR flash: a program ANDs into the part, so a
 * skipped erase corrupts the image.
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
