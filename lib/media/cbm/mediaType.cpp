#ifdef BUILD_IEC

#include "mediaType.h"

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
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "PRG") == 0)
        {
            return MEDIATYPE_PRG;
        }
        else if (strcasecmp(ext, "D64") == 0)
        {
            return MEDIATYPE_D64;
        }
        else if (strcasecmp(ext, "D71") == 0)
        {
            return MEDIATYPE_D71;
        }
        else if (strcasecmp(ext, "D81") == 0)
        {
            return MEDIATYPE_D81;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

#endif /* BUILD_IEC */
