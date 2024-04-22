#ifdef BUILD_MAC
#include "mediaType.h"

MediaType::~MediaType()
{
    unmount();
}

bool MediaType::format(uint16_t *responsesize)
{
    return true;
}

#include <cstdint>
#include <cstring>

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
    // should probably look inside the file to help figure it out
    int l = strlen(filename);
    if (l > 5 && filename[l - 5] == '.')
    {
        // Check the last 4 characters of the string
        const char *ext = filename + l - 4;
        if (strcasecmp(ext, "MOOF") == 0)
            return MEDIATYPE_MOOF;
    }
    else if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "DSK") == 0)
             return MEDIATYPE_DCD; // todo: check size, if 400 or 800k, then floppy, otherwise DCD
        // else if (strcasecmp(ext,"2MG") == 0)
        //     return MEDIATYPE_PO;
        // else if (strcasecmp(ext, "WOZ") == 0)
        //     return MEDIATYPE_WOZ;
        // else if (strcasecmp(ext, "DSK") == 0)
        //     return MEDIATYPE_DSK;
    }
    else if (l > 3 && filename[l - 3] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 2;
        // if (strcasecmp(ext, "PO") == 0)
        //     return MEDIATYPE_PO;
    }
    return MEDIATYPE_UNKNOWN;
}

#endif // BUILD_MAC

#if 0

#include "mediaType.h"

#include <cstdint>
#include <cstring>

MediaType::~MediaType()
{
    unmount();
}

bool MediaType::format(uint16_t *responsesize)
{
    return true;
}

// bool MediaType::read(uint32_t blockNum, uint16_t *readcount)
// {
//     return true;
// }

// bool MediaType::write(uint32_t blockNum, bool verify)
// {
//     return true;
// }

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
    // should probably look inside the file to help figure it out
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "HDV") == 0)
            return MEDIATYPE_PO;
        else if (strcasecmp(ext, "2MG") == 0)
            return MEDIATYPE_PO;
        else if (strcasecmp(ext, "WOZ") == 0)
            return MEDIATYPE_WOZ;
        else if (strcasecmp(ext, "DSK") == 0)
            return MEDIATYPE_DSK;
    }
    else if (l > 3 && filename[l - 3] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 2;
        if (strcasecmp(ext, "PO") == 0)
            return MEDIATYPE_PO;
    }
    return MEDIATYPE_UNKNOWN;
}

#endif // NEW_TARGET