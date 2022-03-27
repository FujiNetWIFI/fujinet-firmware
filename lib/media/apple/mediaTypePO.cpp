#ifdef BUILD_APPLE

#include "mediaTypePO.h"

bool MediaTypePO::read(uint32_t blockNum, uint16_t *readcount)
{
    return false;
}

bool MediaTypePO::write(uint32_t blockNum, bool verify)
{
    return false;
}

bool MediaTypePO::format(uint16_t *respopnsesize)
{
    return false;
}

mediatype_t MediaTypePO::mount(FILE *f, uint32_t disksize)
{
  _media_fileh = f;
  num_blocks = disksize/512;
  return MEDIATYPE_PO;
}


// static bool create(FILE *f, uint32_t numBlock)
// {
//     return false;
// }

#endif // BUILD_APPLE