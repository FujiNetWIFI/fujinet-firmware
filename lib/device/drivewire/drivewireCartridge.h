#ifndef DRIVEWIRECARTRIDGE_H
#define DRIVEWIRECARTRIDGE_H

#include "Cartridge.h"

// pico/fujiversal: the base push as is. It has no memory map.
class drivewireCartridge : public Cartridge
{
public:
    using Cartridge::Cartridge;

    bool is_rom(const char *filename, uint32_t size) override;
    success_is_true mount(fnFile *fileh, const char *filename, uint32_t size,
                          fujiHost &host) override;
};

#endif /* DRIVEWIRECARTRIDGE_H */
