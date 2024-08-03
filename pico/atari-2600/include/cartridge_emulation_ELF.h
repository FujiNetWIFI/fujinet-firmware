#ifndef CARTRIDGE_ELF_H
#define CARTRIDGE_ELF_H

#include "cartridge_io.h"
#include "elfLib.h"

int launch_elf_file(const char* filename, uint32_t buffer_size, uint8_t *buffer);

#endif // CARTRIDGE_ELF_H
