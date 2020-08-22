#ifndef _DISKTYPE_ATX_
#define _DISKTYPE_ATX_

#include "diskType.h"

class DiskTypeATX : public DiskType
{
private:
    struct atxheader
    {
        uint32_t magic;
        uint16_t version;
        uint16_t min_version;
        uint16_t creator;
        uint16_t creator_version;
        uint32_t flags;
        uint16_t image_type;
        uint8_t density;
        uint8_t reserved1;
        uint32_t image_id;
        uint16_t image_version;
        uint16_t reserved2;
        uint32_t start;
        uint32_t end;
    } __attribute__((packed));;

    uint32_t _sector_to_offset(uint16_t sectorNum);

    bool _load_atx_data(FILE *f, atxheader *header);

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual disktype_t mount(FILE *f, uint32_t disksize) override;

    virtual uint16_t sector_size(uint16_t sectornum) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(FILE *f, uint16_t sectorSize, uint16_t numSectors);
};


#endif // _DISKTYPE_ATX_
