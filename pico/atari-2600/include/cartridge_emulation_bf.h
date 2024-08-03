#ifndef CARTRIDGE_EMULATION_BF_H
#define CARTRIDGE_EMULATION_BF_H

#include <stdint.h>

void emulate_bf_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d);

void emulate_bfsc_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d);

#endif // CARTRIDGE_EMULATION_BF_H
