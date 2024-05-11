#ifndef _MEDIATYPE_DCD_
#define _MEDIATYPE_DCD_

#include <stdio.h>

#include "mediaType.h"

class MediaTypeDCD : public MediaType
{
private:
    uint32_t last_block_num = 0xFFFFFFFF;
    uint32_t offset = 0;
public:
    virtual bool read(uint32_t blockNum, uint8_t* buffer) override;
    virtual bool write(uint32_t blockNum,  uint8_t* buffer) override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    mediatype_t mount(FILE *f) { return mount(f, 0); };
    
    virtual bool status() override {return (_media_fileh != nullptr);}

    // static bool create(FILE *f, uint32_t numBlock);

    size_t size() {return _media_num_sectors;}
    size_t sectorsize() {return _media_sector_size;}
    void reset_seek_opto() {last_block_num = 0xFFFFFFFF;};

    MediaTypeDCD(int x = 0) : offset(x) {}
};


#endif // _MEDIATYPE_DCD_
