#ifndef _MEDIATYPE_DSK_
#define _MEDIATYPE_DSK_

#include <utility>
#include "mediaType.h"

class MediaTypeDSK : public MediaType
{
private:
    std::pair<uint32_t, uint32_t> _block_to_offsets(uint32_t blockNum);

public:
    virtual bool read(uint32_t blockNum, uint16_t *readcount) override;
    virtual bool write(uint16_t blockNum, bool verify) override;

    virtual bool format(uint16_t *respopnsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(FILE *f, uint32_t numBlock);
};


#endif // _DISKTYPE_DSK_