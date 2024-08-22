#ifndef _MEDIATYPE_MRM_
#define _MEDIATYPE_MRM_

#include <stdio.h>

#include "mediaType.h"

#define MRM_BLOCK_SIZE  32768

class MediaTypeMRM : public MediaType
{
private:
    uint32_t _block_to_offset(uint32_t blockNum);

public:
    uint8_t _media_blockbuff[MRM_BLOCK_SIZE];

    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint32_t blockNum, bool verify) override;

    virtual void get_block_buffer(uint8_t **p_buffer, uint16_t *p_blk_size) override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;

    virtual uint8_t status() override;

    static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_MRM_
