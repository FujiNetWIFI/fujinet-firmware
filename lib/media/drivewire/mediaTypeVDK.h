#ifndef _MEDIATYPE_VDK_
#define _MEDIATYPE_VDK_

#include <stdio.h>

#include "mediaType.h"

#define VDK_BLOCK_SIZE  256

class MediaTypeVDK : public MediaType
{
private:
    uint32_t _block_to_offset(uint32_t blockNum);
    bool bIsVDK;
    uint16_t _media_sector_size = VDK_BLOCK_SIZE;

public:
    uint8_t _media_blockbuff[VDK_BLOCK_SIZE];
    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint32_t blockNum, bool verify) override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;

    virtual uint8_t status() override;

    static bool create(FILE *f, uint32_t numBlock);

    void get_block_buffer(uint8_t **p_buffer, uint16_t *p_blk_size) override;
};


#endif // _MEDIATYPE_VDK_
