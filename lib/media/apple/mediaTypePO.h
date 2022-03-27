#ifndef _MEDIATYPE_PO_
#define _MEDIATYPE_PO_

#include <stdio.h>

#include "mediaType.h"

class MediaTypePO : public MediaType
{
public:
    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint32_t blockNum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;

    virtual bool status() override {return (_media_fileh != nullptr);}

    // static bool create(FILE *f, uint32_t numBlock);

    size_t size() {return _media_num_sectors;}
};


#endif // _MEDIATYPE_PO_
