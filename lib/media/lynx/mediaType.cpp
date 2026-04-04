#ifdef BUILD_LYNX

#include "mediaType.h"

#include <cstdint>
#include <cstring>


MediaType::~MediaType()
{
    unmount();
}

error_is_true MediaType::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaType::read(uint32_t blockNum, uint16_t *readcount)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaType::write(uint32_t blockNum, bool verify)
{
    RETURN_ERROR_AS_TRUE();
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
    // On the Lynx, we can just treat every file like a ROM file
    // We aren't doing anything special with the files at this point
    // but might need to change in future.

    /*
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if ((strcasecmp(ext, "LNX") == 0) || (strcasecmp(ext, "LYX") = 0))
        {
            return MEDIATYPE_ROM;
        }
    }
    return MEDIATYPE_UNKNOWN;*/

    return MEDIATYPE_ROM;
}

#endif /* BUILD_LYNX */
