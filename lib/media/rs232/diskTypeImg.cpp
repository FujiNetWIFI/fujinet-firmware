#ifdef BUILD_RS232 // temporary

#include "diskTypeImg.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "utils.h"


// Returns byte offset of given sector number
uint32_t MediaTypeImg::_sector_to_offset(uint32_t sectorNum)
{
    return (uint32_t )sectorNum * 512;
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeImg::read(uint32_t sectornum, uint32_t *readcount)
{
    Debug_print("IMG READ\r\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    // (or there's no disk: unmount() closes the file)
    if (_disk_fileh == nullptr || sectornum >= _disk_num_sectors)
    {
        Debug_printf("::read sector %ld >= %lu\r\n", sectornum, _disk_num_sectors);
        RETURN_ERROR_AS_TRUE();
    }

    uint16_t sectorSize = sector_size(sectornum);

    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _disk_last_sector + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fnio::fseek(_disk_fileh, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fnio::fread(_disk_sectorbuff, 1, sectorSize, _disk_fileh) != sectorSize;

    if (err == false)
        _disk_last_sector = sectornum;
    else
        _disk_last_sector = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    RETURN_ERROR_IF(err);
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeImg::write(uint32_t sectornum, bool verify)
{
    Debug_printf("IMG WRITE\r\n");

    // Return an error if we're trying to write beyond the end of the disk
    // (which would grow the file), or there's no disk
    if (_disk_fileh == nullptr || sectornum >= _disk_num_sectors)
    {
        Debug_printf("::write sector %ld >= %lu\r\n", sectornum, _disk_num_sectors);
        RETURN_ERROR_AS_TRUE();
    }

    uint16_t sectorSize = sector_size(sectornum);
    uint32_t offset = _sector_to_offset(sectornum);

    _disk_last_sector = INVALID_SECTOR_VALUE;

    // Perform a seek if we're not writing the sector after the last one we wrote
    int e;
    if (sectornum != _disk_last_sector + 1)
    {
        e = fnio::fseek(_disk_fileh, offset, SEEK_SET);
        if (e != 0)
        {
            Debug_printf("::write seek error %d\r\n", e);
            RETURN_ERROR_AS_TRUE();
        }
    }
    // Write the data
    e = fnio::fwrite(_disk_sectorbuff, 1, sectorSize, _disk_fileh);
    if (e != sectorSize)
    {
        Debug_printf("::write error %d, %d\r\n", e, errno);
        RETURN_ERROR_AS_TRUE();
    }

    int ret = fnio::fflush(_disk_fileh); // Since we might get reset at any moment, go ahead and sync the file
    Debug_printf("IMG::write fflush:%d\r\n", ret);

    _disk_last_sector = sectornum;

    RETURN_SUCCESS_AS_FALSE();
}

void MediaTypeImg::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_disk_sector_size > 128)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_SIDED;

    if (_percomBlock.sectors_per_track == 26)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeImg::format(uint32_t *responsesize)
{
    Debug_print("IMG FORMAT\r\n");

    // Populate an empty bad sector map
    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));
    _disk_sectorbuff[0] = 0xFF;
    _disk_sectorbuff[1] = 0xFF;

    *responsesize = _disk_sector_size;

    RETURN_SUCCESS_AS_FALSE();
}

mediatype_t MediaTypeImg::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("IMG MOUNT\r\n");

    _disk_fileh = f;
    _disk_num_sectors = disksize / 512;
    _disktype = MEDIATYPE_IMG;

    return _disktype;
}

// Returns FALSE on error
success_is_true MediaTypeImg::create(fnFile *f, uint16_t sectorSize, uint32_t numSectors)
{
    uint8_t blank[512] = {0};
    uint64_t remaining = (uint64_t) sectorSize * numSectors;

    Debug_print("IMG CREATE\r\n");
    if (remaining == 0)
        RETURN_ERROR_AS_FALSE();

    while (remaining)
    {
        size_t n = remaining < sizeof(blank) ? remaining : sizeof(blank);
        if (fnio::fwrite(blank, 1, n, f) != n)
            RETURN_ERROR_AS_FALSE();
        remaining -= n;
    }
    RETURN_SUCCESS_AS_TRUE();
}
#endif /* BUILD_RS232 */
