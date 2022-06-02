#ifndef _MEDIATYPE_ROM_
#define _MEDIATYPE_ROM_

#include <stdio.h>

#include "mediaType.h"


class MediaTypeROM : public MediaType
{
    uint32_t _block_to_offset(uint32_t blockNum);

public:
    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint32_t blockNum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;

    virtual uint8_t status() override;

    static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_ROM_