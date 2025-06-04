#ifndef _MEDIATYPE_IMG
#define _MEDIATYPE_IMG

#include "diskType.h"

class MediaTypeImg : public MediaType
{
private:
    uint32_t _sector_to_offset(uint32_t sectorNum);

public:
    virtual bool read(uint32_t sectornum, uint32_t *readcount) override;
    virtual bool write(uint32_t sectornum, bool verify) override;

    virtual bool format(uint32_t *responsesize) override;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(fnFile *f, uint16_t sectorSize, uint32_t numSectors);
};


#endif // _MEDIATYPE_IMG
