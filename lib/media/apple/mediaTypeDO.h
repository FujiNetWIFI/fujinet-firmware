#ifndef _MEDIATYPE_DO_
#define _MEDIATYPE_DO_

#include <stdio.h>

#include "mediaType.h"

class MediaTypeDO : public MediaType
{
private:
    error_is_true read_sector(int track, int sector, uint8_t* buffer);
    error_is_true write_sector(int track, int sector, uint8_t* buffer) override;

public:
    error_is_true read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;
    error_is_true write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    success_is_true status() override {RETURN_SUCCESS_IF(_media_fileh != nullptr);}
};


#endif // _MEDIATYPE_DO_
