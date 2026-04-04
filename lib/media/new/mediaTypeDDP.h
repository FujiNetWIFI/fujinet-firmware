#ifndef _MEDIATYPE_DDP_
#define _MEDIATYPE_DDP_

#include <stdio.h>

#include "mediaType.h"

class MediaTypeDDP : public MediaType
{
private:
    uint32_t _block_to_offset(uint32_t blockNum);

public:
    success_is_true read(uint32_t blockNum, uint16_t *readcount) override;
    success_is_true write(uint32_t blockNum, bool verify) override;

    success_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(FILE *f, uint32_t disksize) override;

    uint8_t status() override;

    static success_is_true create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_DDP_
