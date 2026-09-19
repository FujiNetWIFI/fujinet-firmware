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

    // Returns the size of the ROM image; 0 if unavailable.
    uint32_t stream_size();

    // Returns bytes copied; 0 at end of stream or on error.
    size_t stream_read(uint32_t offset, uint8_t *buffer, size_t length);

private:
    uint32_t _image_pos = UINT32_MAX; // forces a seek on the first read
};


#endif // _MEDIATYPE_ROM_
