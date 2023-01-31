#ifdef BUILD_RS232 // temporary

#include "diskType.h"

#include <string.h>

#include "../../include/debug.h"

#include "utils.h"


#define DENSITY_FM 0
#define DENSITY_MFM 4

#define SIDES_SS 0
#define SIDES_DS 1

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t MediaType::sector_size(uint16_t sectornum)
{
    if (_disk_sector_size == 512)
        return 512;
    else
        return sectornum <= 3 ? 128 : _disk_sector_size;
}

// Default WRITE is not implemented
bool MediaType::write(uint16_t sectornum, bool verify)
{
    Debug_print("DISK WRITE NOT IMPLEMENTED\n");
    return true;
}

// Default FORMAT is not implemented
bool MediaType::format(uint16_t *responsesize)
{
    Debug_print("DISK FORMAT NOT IMPLEMENTED\n");
    return true;
}

// Update PERCOM block from the total # of sectors
void MediaType::derive_percom_block(uint16_t numSectors)
{

}

// Dump PERCOM block
void MediaType::dump_percom_block()
{

}

void MediaType::unmount()
{
    if (_disk_fileh != nullptr)
    {
        fclose(_disk_fileh);
        _disk_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_disktype(const char *filename)
{
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "XEX") == 0)
        {
            return MEDIATYPE_IMG;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

MediaType::~MediaType()
{
    unmount();
}

#endif /* BUILD_RS232 */