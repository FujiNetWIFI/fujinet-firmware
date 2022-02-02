#ifdef NEW_TARGET

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
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "DDP") == 0)
        {
            return MEDIATYPE_DDP;
        }
        else if (strcasecmp(ext, "DSK") == 0)
        {
            return MEDIATYPE_DSK;
        }
        else if (strcasecmp(ext, "ROM") == 0)
        {
            return MEDIATYPE_ROM;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

#endif /* NEW_TARGET */