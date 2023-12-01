#ifndef _MEDIATYPE_ATR_
#define _MEDIATYPE_ATR_

#include "diskType.h"

class MediaTypeATR : public MediaType
{
private:
    uint32_t _sector_to_offset(uint16_t sectorNum);

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

#ifdef ESP_PLATFORM
    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
#else
    virtual mediatype_t mount(FileHandler *f, uint32_t disksize) override;
#endif

    virtual void status(uint8_t statusbuff[4]) override;

#ifdef ESP_PLATFORM
    static bool create(FILE *f, uint16_t sectorSize, uint16_t numSectors);
#else
    static bool create(FileHandler *f, uint16_t sectorSize, uint16_t numSectors);
#endif
};


#endif // _MEDIATYPE_ATR_
