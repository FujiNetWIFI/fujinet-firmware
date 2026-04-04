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

    error_is_true read(uint32_t blockNum, uint16_t *readcount) override;
    error_is_true write(uint32_t blockNum, bool verify) override;

    void get_block_buffer(uint8_t **p_buffer, uint16_t *p_blk_size) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    uint8_t status() override;

    static success_is_true create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_MRM_
