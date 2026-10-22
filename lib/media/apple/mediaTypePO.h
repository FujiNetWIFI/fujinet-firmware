#ifndef _MEDIATYPE_PO_
#define _MEDIATYPE_PO_

#include <stdio.h>

#include "mediaType.h"

class MediaTypePO : public MediaType
{
private:
    uint32_t last_block_num = 0xFFFFFFFF;
protected:
    uint32_t offset = 0; // where block 0 starts in the file
public:
    error_is_true read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;
    error_is_true write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override;
    error_is_true write_sector(int track, int sector, uint8_t *buffer) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    success_is_true status() override {RETURN_SUCCESS_IF(_media_fileh != nullptr);}


    size_t size() {return _media_num_sectors;}
    void reset_seek_opto() {last_block_num = 0xFFFFFFFF;};
};


#endif // _MEDIATYPE_PO_
