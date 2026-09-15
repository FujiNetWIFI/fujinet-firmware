#ifndef _MEDIATYPE_ROM_
#define _MEDIATYPE_ROM_

#include <stdio.h>

#include "mediaType.h"

class MediaTypeROM : public MediaType
{
public:
    error_is_true read(uint32_t blockNum, uint16_t *readcount) override;
    error_is_true write(uint32_t blockNum, bool verify) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    uint8_t status() override;

#if defined(PINMAP_FUJIVERSAL_DRIVEWIRE) || defined(COCO_HS_UART)
    // Pushes this object's already-mounted file to the DBC device over the
    // FujiBusPacket/SLIP side channel - see mediaTypeROM.cpp.
    bool push_stream();
#endif
};


#endif // _MEDIATYPE_ROM_
