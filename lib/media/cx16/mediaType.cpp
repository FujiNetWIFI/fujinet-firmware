#ifdef BUILD_CX16

#include "mediaType.h"

#include <cstdint>
#include <cstring>


MediaType::~MediaType()
{
    unmount();
}

bool MediaType::format(uint16_t *respopnsesize)
{
    return true;
}

bool MediaType::read(uint32_t blockNum, uint16_t *readcount)
{
    return true;
}

bool MediaType::write(uint32_t blockNum, bool verify)
{
    return true;
}

void MediaType::unmount()
{
    if (_media_fileh != nullptr)
    {
        fclose(_media_fileh);
        _media_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_mediatype(const char *filename)
{
    return MEDIATYPE_UNKNOWN;
}

#endif /* BUILD_CX16 */