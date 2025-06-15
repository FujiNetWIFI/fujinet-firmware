#ifndef _MEDIATYPE_DO_
#define _MEDIATYPE_DO_

#include <stdio.h>

#include "mediaType.h"

class MediaTypeDO : public MediaType
{
private:
    bool read_sector(int track, int sector, uint8_t* buffer);
    bool write_sector(int track, int sector, uint8_t* buffer) override;

public:
    virtual bool read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;
    virtual bool write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;

    virtual bool status() override {return (_media_fileh != nullptr);}
};


#endif // _MEDIATYPE_DO_
