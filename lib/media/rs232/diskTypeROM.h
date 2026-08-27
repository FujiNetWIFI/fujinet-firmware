#ifndef _MEDIATYPE_ROM_RS232
#define _MEDIATYPE_ROM_RS232

#include "diskType.h"

// Pushes a ROM-shaped file (Intellivision .bin+.cfg pair, Intellicart
// .rom, Atari 2600 .bin, CoCo cartridge image, ...) to the RP2040 over the
// same USB-CDC/FujiBus link used for ordinary mailbox transactions,
// addressed to FUJI_DEVICEID::DBC. See diskTypeROM.cpp for the wire
// sequence.
class MediaTypeROM : public MediaType
{
public:
    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;

    error_is_true format(uint32_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize, fujiHost *host = nullptr,
                      const char *filename = nullptr) override;

    void status(uint8_t statusbuff[4]) override;
};

#endif // _MEDIATYPE_ROM_RS232
