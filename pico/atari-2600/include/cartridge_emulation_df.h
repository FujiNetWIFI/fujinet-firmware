#ifndef CARTRIDGE_EMULATION_DF_H
#define CARTRIDGE_EMULATION_DF_H

#include <stdint.h>

void emulate_df_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d);

void emulate_dfsc_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d);

#endif // CARTRIDGE_EMULATION_DF_H
