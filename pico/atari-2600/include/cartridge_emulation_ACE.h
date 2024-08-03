#ifndef CARTRIDGE_ACE_H
#define CARTRIDGE_ACE_H

int launch_ace_cartridge(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d, bool withPlusFunctions, uint8_t *eram);

int is_ace_cartridge(unsigned int image_size, uint8_t *buffer);

#endif // CARTRIDGE_ACE_H
