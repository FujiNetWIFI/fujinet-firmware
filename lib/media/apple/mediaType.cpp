#ifdef BUILD_APPLE

#include "mediaType.h"
#include "utils.h"
#include "endianness.h"

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
        fnio::fclose(_media_fileh);
        _media_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_mediatype(const char *filename)
{
    //should probably look inside the file to help figure it out
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "HDV") == 0)
            return MEDIATYPE_PO;
        else if (strcasecmp(ext,"2MG") == 0)
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
        else if (strcasecmp(ext, "DO") == 0)
            return MEDIATYPE_DO;
    }
    return MEDIATYPE_UNKNOWN;
}

mediatype_t MediaType::discover_dsk_mediatype(fnFile *f, uint32_t disksize)
{
    mediatype_t default_mt = MEDIATYPE_DO;

    const size_t header_size = 64;
    uint8_t hdr[header_size];

    // a ProDOS Volume Directory Header is always in block 2,
    // if the file is in ProDOS order, it will be at offset 1024
    if (fnio::fseek(f, 1024, SEEK_SET) != 0)
        return default_mt;

    if (fnio::fread(hdr, 1, header_size, f) != header_size)
        return default_mt;

    uint16_t prevPointer = UINT16_FROM_HILOBYTES(hdr[1], hdr[0]);
    uint8_t  storage_type = hdr[4] >> 4;
    uint16_t total_blocks = UINT16_FROM_HILOBYTES(hdr[0x2A], hdr[0x29]);

    if (prevPointer == 0 && storage_type == 0xF && total_blocks == (disksize / 512)) {
        // looks like a valid volume header, assume file is in ProDOS order
        return MEDIATYPE_PO;
    }

    return default_mt;
}

#endif // BUILD_APPLE
