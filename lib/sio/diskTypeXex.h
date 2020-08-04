#ifndef _DISKTYPE_XEX_
#define _DISKTYPE_XEX_

#include "diskType.h"

class DiskTypeXEX : public DiskType
{
private:
    uint8_t *_bootloader = nullptr;
    int _bootloadersize = 0;

    void _fake_directory_entry();

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override { return true; };

    virtual bool format(uint16_t *respopnsesize) override { return true; };

    virtual disktype_t mount(FILE *f, uint32_t disksize) override;
    virtual void unmount() override;

    virtual uint16_t sector_size(uint16_t sectornum) override;

    virtual void status(uint8_t statusbuff[4]) override;

    ~DiskTypeXEX();
};

#endif // _DISKTYPE_XEX_
