#ifdef BUILD_ATARI // temporary

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
    // Start with 40T/1S 720 sectors, sector size passed in
    _percomBlock.num_tracks = 40;
    _percomBlock.step_rate = 1;
    _percomBlock.sectors_per_trackH = 0;
    _percomBlock.sectors_per_trackL = 18;
    _percomBlock.num_sides = SIDES_SS;
    _percomBlock.density = DENSITY_FM;
    _percomBlock.sector_sizeH = HIBYTE_FROM_UINT16(_disk_sector_size);
    _percomBlock.sector_sizeL = LOBYTE_FROM_UINT16(_disk_sector_size);
    _percomBlock.drive_present = 255;
    _percomBlock.reserved1 = 0;
    _percomBlock.reserved2 = 0;
    _percomBlock.reserved3 = 0;

    if (numSectors == 1040) // 5.25" 1050 density
    {
        _percomBlock.sectors_per_trackL = 26;
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 720 && _disk_sector_size == 256) // 5.25" SS/DD
    {
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 1440) // 5.25" DS/DD
    {
        _percomBlock.num_sides = SIDES_DS;
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 2880) // 5.25" DS/QD
    {
        _percomBlock.num_sides = SIDES_DS;
        _percomBlock.num_tracks = 80;
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 2002 && _disk_sector_size == 128) // SS/SD 8"
    {
        _percomBlock.num_tracks = 77;
    }
    else if (numSectors == 2002 && _disk_sector_size == 256) // SS/DD 8"
    {
        _percomBlock.num_tracks = 77;
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 4004 && _disk_sector_size == 128) // DS/SD 8"
    {
        _percomBlock.num_tracks = 77;
    }
    else if (numSectors == 4004 && _disk_sector_size == 256) // DS/DD 8"
    {
        _percomBlock.num_sides = SIDES_DS;
        _percomBlock.num_tracks = 77;
        _percomBlock.density = DENSITY_MFM;
    }
    else if (numSectors == 5760) // 1.44MB 3.5" High Density
    {
        _percomBlock.num_sides = SIDES_DS;
        _percomBlock.num_tracks = 80;
        _percomBlock.sectors_per_trackL = 36;
        _percomBlock.density = 8; // I think this is right.
    }
    else
    {
        // This is a custom size, one long track.
        _percomBlock.num_tracks = 1;
        _percomBlock.sectors_per_trackH = HIBYTE_FROM_UINT16(numSectors);
        _percomBlock.sectors_per_trackL = LOBYTE_FROM_UINT16(numSectors);
    }

#ifdef VERBOSE_DISK
    dump_percom_block();
#endif
}

// Dump PERCOM block
void MediaType::dump_percom_block()
{
#ifdef VERBOSE_DISK
    Debug_printf("Percom Block Dump\n");
    Debug_printf("-----------------\n");
    Debug_printf("Num Tracks: %d\n", _percomBlock.num_tracks);
    Debug_printf("Step Rate: %d\n", _percomBlock.step_rate);
    Debug_printf("Sectors per Track: %d\n", (_percomBlock.sectors_per_trackH * 256 + _percomBlock.sectors_per_trackL));
    Debug_printf("Num Sides: %d\n", _percomBlock.num_sides);
    Debug_printf("Density: %d\n", _percomBlock.density);
    Debug_printf("Sector Size: %d\n", (_percomBlock.sector_sizeH * 256 + _percomBlock.sector_sizeL));
    Debug_printf("Drive Present: %d\n", _percomBlock.drive_present);
    Debug_printf("Reserved1: %d\n", _percomBlock.reserved1);
    Debug_printf("Reserved2: %d\n", _percomBlock.reserved2);
    Debug_printf("Reserved3: %d\n", _percomBlock.reserved3);
#endif
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
            return MEDIATYPE_XEX;
        }
        else if (strcasecmp(ext, "COM") == 0)
        {
            return MEDIATYPE_XEX;
        }
        else if (strcasecmp(ext, "BIN") == 0)
        {
            return MEDIATYPE_XEX;
        }
        else if (strcasecmp(ext, "ATR") == 0)
        {
            return MEDIATYPE_ATR;
        }
        else if (strcasecmp(ext, "ATX") == 0)
        {
            return MEDIATYPE_ATX;
        }
        else if (strcasecmp(ext, "CAS") == 0)
        {
            return MEDIATYPE_CAS;
        }
        else if (strcasecmp(ext, "WAV") == 0)
        {
            return MEDIATYPE_WAV;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

MediaType::~MediaType()
{
    unmount();
}

#endif /* BUILD_ATARI */