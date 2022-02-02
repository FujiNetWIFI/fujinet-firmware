#ifndef _MEDIATYPE_XEX_
#define _MEDIATYPE_XEX_

#include "diskType.h"

class MediaTypeXEX : public MediaType
{
private:
    uint8_t *_xex_bootloader = nullptr;
    int _xex_bootloadersize = 0;

    void _fake_directory_entry();

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    virtual void unmount() override;

    virtual void status(uint8_t statusbuff[4]) override;

    ~MediaTypeXEX();
};

#endif // _MEDIATYPE_XEX_
