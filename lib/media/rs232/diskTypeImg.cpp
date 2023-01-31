#ifdef BUILD_RS232 // temporary

#include "diskTypeImg.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "disk.h"
#include "fnSystem.h"

#include "utils.h"


// Returns byte offset of given sector number (1-based)
uint32_t MediaTypeImg::_sector_to_offset(uint16_t sectorNum)
{
    return (uint32_t )sectorNum * 512;
}

// Returns TRUE if an error condition occurred
bool MediaTypeImg::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("IMG READ\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if (sectornum > _disk_num_sectors)
    {
        Debug_printf("::read sector %d > %d\n", sectornum, _disk_num_sectors);
        return true;
    }

    uint16_t sectorSize = sector_size(sectornum);

    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _disk_last_sector + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fseek(_disk_fileh, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fread(_disk_sectorbuff, 1, sectorSize, _disk_fileh) != sectorSize;

    if (err == false)
        _disk_last_sector = sectornum;
    else
        _disk_last_sector = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeImg::write(uint16_t sectornum, bool verify)
{
    Debug_printf("IMG WRITE\n", sectornum, _disk_num_sectors);

    // Return an error if we're trying to write beyond the end of the disk
    if (sectornum > _disk_num_sectors)
    {
        Debug_printf("::write sector %d > %d\n", sectornum, _disk_num_sectors);
        return true;
    }

    uint16_t sectorSize = sector_size(sectornum);
    uint32_t offset = _sector_to_offset(sectornum);

    _disk_last_sector = INVALID_SECTOR_VALUE;

    // Perform a seek if we're writing to the sector after the last one
    int e;
    if (sectornum != _disk_last_sector + 1)
    {
        e = fseek(_disk_fileh, offset, SEEK_SET);
        if (e != 0)
        {
            Debug_printf("::write seek error %d\n", e);
            return true;
        }
    }
    // Write the data
    e = fwrite(_disk_sectorbuff, 1, sectorSize, _disk_fileh);
    if (e != sectorSize)
    {
        Debug_printf("::write error %d, %d\n", e, errno);
        return true;
    }

    int ret = fflush(_disk_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_disk_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("IMG::write fsync:%d\n", ret);

    _disk_last_sector = sectornum;

    return false;
}

void MediaTypeImg::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_disk_sector_size > 128)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_SIDED;

    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

/*
    From Altirra manual:
    The format command formats a disk, writing 40 tracks and then verifying all sectors.
    All sectors are filleded with the data byte $00. On completion, the drive returns
    a sector-sized buffer containing a list of 16-bit bad sector numbers terminated by $FFFF.
*/
// Returns TRUE if an error condition occurred
bool MediaTypeImg::format(uint16_t *responsesize)
{
    Debug_print("IMG FORMAT\n");

    // Populate an empty bad sector map
    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));
    _disk_sectorbuff[0] = 0xFF;
    _disk_sectorbuff[1] = 0xFF;

    *responsesize = _disk_sector_size;

    return false;
}

/* 
 Mount ATR disk
 Header layout:
 00 lobyte 0x96
 01 hibyte 0x02
 02 lobyte paragraphs (16-byte blocks) on disk
 03 hibyte
 04 lobyte sector size (0x80, 0x100, etc.)
 05 hibyte
 06   byte paragraphs on disk extension (24-bits total)
 
 07-0F have two possible interpretations but are no critical for our use
*/
mediatype_t MediaTypeImg::mount(FILE *f, uint32_t disksize)
{
    Debug_print("IMG MOUNT\n");

    _disk_fileh = f;
    _disk_num_sectors = disksize / 512;
    _disktype = MEDIATYPE_IMG;

    return _disktype;
}

// Returns FALSE on error
bool MediaTypeImg::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    return true;
}
#endif /* BUILD_ATARI */