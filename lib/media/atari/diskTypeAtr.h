#ifndef _DISKTYPE_ATR_
#define _DISKTYPE_ATR_

#include "diskType.h"

class DiskTypeATR : public DiskType
{
private:
    uint32_t _sector_to_offset(uint16_t sectorNum);

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual disktype_t mount(FILE *f, uint32_t disksize) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(FILE *f, uint16_t sectorSize, uint16_t numSectors);
};


#endif // _DISKTYPE_ATR_
