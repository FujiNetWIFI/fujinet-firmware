#ifndef CARTRIDGE_EMULATION_AR_H
#define CARTRIDGE_EMULATION_AR_H

// multicore wrapper
void _emulate_ar_cartridge(void);

void __time_critical_func(emulate_ar_cartridge)(const char* filename, unsigned int image_size, uint8_t *buffer, int tv_mode, MENU_ENTRY *d);

#endif // CARTRIDGE_EMULATION_AR_H
