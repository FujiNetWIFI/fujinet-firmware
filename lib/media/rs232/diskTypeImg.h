#ifndef _MEDIATYPE_IMG
#define _MEDIATYPE_IMG

#include "diskType.h"

class MediaTypeImg : public MediaType
{
private:
    uint32_t _sector_to_offset(uint32_t sectorNum);

public:
    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;

    error_is_true format(uint32_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    void status(uint8_t statusbuff[4]) override;

    static success_is_true create(fnFile *f, uint16_t sectorSize, uint32_t numSectors);
};


#endif // _MEDIATYPE_IMG
