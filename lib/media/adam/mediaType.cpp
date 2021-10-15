#ifdef BUILD_ADAM

#include <cstring>
#include "mediaType.h"

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

#endif /* BUILD_ADAM */