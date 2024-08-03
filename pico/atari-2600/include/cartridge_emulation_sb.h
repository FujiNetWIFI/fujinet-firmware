#ifndef CARTRIDGE_EMULATION_SB_H
#define CARTRIDGE_EMULATION_SB_H

#include <stdint.h>

/* SB SUPERBanking  */

void emulate_SB_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d);

#endif // CARTRIDGE_EMULATION_SB_H
