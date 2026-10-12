#ifdef BUILD_RS232

#include "diskTypeAltair.h"

#include <string.h>

#include "../../include/debug.h"

// Deliberately no disk.h/bus.h/fnSystem.h, so tests/MediaTypeAltairTests.cpp
// links this file without a bus.

struct AltairGeometry
{
    uint32_t size;
    uint16_t tracks;
    uint16_t spt;
};

// The larger 8" and minidisk sizes carry the padding left by the tools that
// made most published images; the padding is never addressed.
static const AltairGeometry altair_geometries[] = {
    {337568, 77, 32},    // Altair 8"
    {337664, 77, 32},
    {76720, 35, 16},     // Altair minidisk
    {76800, 35, 16},
    {8978432, 2048, 32}, // FDC+ 8 MB serial drive
};

static const AltairGeometry *altair_geometry(uint32_t disksize)
{
    for (const AltairGeometry &g : altair_geometries)
        if (g.size == disksize)
            return &g;
    return nullptr;
}

bool MediaTypeAltair::looks_like_altair(const char *filename, uint32_t disksize)
{
    if (filename == nullptr || altair_geometry(disksize) == nullptr)
        return false;
    size_t l = strlen(filename);
    return l > 4 && filename[l - 4] == '.' && strcasecmp(filename + l - 3, "DSK") == 0;
}

mediatype_t MediaTypeAltair::mount(fnFile *f, uint32_t disksize)
{
    // Take the handle first: on a failed mount the caller leaves the file open
    // and relies on this MediaType being deleted to close it.
    _disk_fileh = f;
    _disk_image_size = disksize;

    const AltairGeometry *g = altair_geometry(disksize);
    if (g == nullptr)
    {
        Debug_printf("ALTAIR MOUNT refused: %lu bytes is not an FDC+ image size\r\n",
                     (unsigned long)disksize);
        return MEDIATYPE_UNKNOWN;
    }

    _tracks = g->tracks;
    _spt = g->spt;
    _disk_num_sectors = (uint32_t)_tracks * _spt;
    _disk_sector_size = ALTAIR_SECTOR_SIZE;
    _disk_controller_status = DISK_CTRL_STATUS_CLEAR;
    _disktype = MEDIATYPE_ALTAIR;

    // num_tracks is a byte: 0 stands for the 2048-track 8 MB drive.
    memset(&_percomBlock, 0, sizeof(_percomBlock));
    _percomBlock.num_tracks = (uint8_t)(_tracks > 255 ? 0 : _tracks);
    _percomBlock.step_rate = 0x01;
    _percomBlock.sectors_per_track = _spt;
    _percomBlock.density = DENSITY_FM;
    _percomBlock.sector_size = ALTAIR_SECTOR_SIZE;
    _percomBlock.drive_present = 0xFF;

    Debug_printf("ALTAIR MOUNT %s: %u tracks x %u sectors, %s\r\n", _disk_filename, _tracks,
                 _spt, _writable ? "rw" : "ro");

    return _disktype;
}

error_is_true MediaTypeAltair::_seek(uint32_t sectornum)
{
    if (sectornum >= _disk_num_sectors)
    {
        Debug_printf("ALTAIR sector %lu >= %lu\r\n", (unsigned long)sectornum,
                     (unsigned long)_disk_num_sectors);
        _disk_controller_status = DISK_CTRL_STATUS_SECTOR_MISSING;
        RETURN_ERROR_AS_TRUE();
    }
    RETURN_ERROR_IF(fnio::fseek(_disk_fileh, sectornum * ALTAIR_SECTOR_SIZE, SEEK_SET) != 0);
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeAltair::read(uint32_t sectornum, uint32_t *readcount)
{
    *readcount = 0;
    _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

    if (_seek(sectornum).is_error())
        RETURN_ERROR_AS_TRUE();

    if (fnio::fread(_disk_sectorbuff, 1, ALTAIR_SECTOR_SIZE, _disk_fileh) != ALTAIR_SECTOR_SIZE)
    {
        _disk_controller_status = DISK_CTRL_STATUS_DATA_LOST;
        RETURN_ERROR_AS_TRUE();
    }

    *readcount = ALTAIR_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeAltair::write(uint32_t sectornum, bool verify)
{
    _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

    if (!_writable)
    {
        _disk_controller_status = DISK_CTRL_STATUS_WRITE_PROTECT_ERROR;
        RETURN_ERROR_AS_TRUE();
    }

    if (_seek(sectornum).is_error())
        RETURN_ERROR_AS_TRUE();

    if (fnio::fwrite(_disk_sectorbuff, 1, ALTAIR_SECTOR_SIZE, _disk_fileh) != ALTAIR_SECTOR_SIZE)
    {
        _disk_controller_status = DISK_CTRL_STATUS_DATA_LOST;
        RETURN_ERROR_AS_TRUE();
    }

    // Since we might get reset at any moment, sync the file
    fnio::fflush(_disk_fileh);
    RETURN_SUCCESS_AS_FALSE();
}

uint16_t MediaTypeAltair::sector_size(uint32_t sectornum)
{
    return ALTAIR_SECTOR_SIZE;
}

void MediaTypeAltair::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (!_writable)
        statusbuff[0] |= DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

#endif /* BUILD_RS232 */
