#ifdef BUILD_ATARI // temporary

#include "diskTypeAtr.h"

#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "../../include/debug.h"

#include "disk.h"
#include "fnSystem.h"

#include "utils.h"
#include "endianness.h"

#define ATR_MAGIC_HEADER 0x0296 // Sum of 'NICKATARI'

// Returns byte offset of given sector number (1-based)
uint32_t MediaTypeATR::_sector_to_offset(uint16_t sectorNum)
{
    uint32_t offset = 0;

    switch (sectorNum)
    {
    case 1:
        offset = 16;
        break;
    case 2:
        if (_disk_sector_size == 512)
            offset = 528;
        else
            offset = 144;
        break;
    case 3:
        if (_disk_sector_size == 512)
            offset = 1040;
        else
            offset = 272;
        break;
    default: // TODO: refactor this to generalize.
        if (_disk_sector_size == 256)
            offset = ((sectorNum - 3) * 256) + 16 + 128;
        else if (_disk_sector_size == 512)
            offset = ((sectorNum - 1) * 512) + 16;
        else
            offset = ((sectorNum - 1) * 128) + 16;
        break;
    }

    return offset;
}

// Returns TRUE if an error condition occurred
bool MediaTypeATR::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_printf("ATR READ %d / %lu\r\n", sectornum, _disk_num_sectors);

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if (sectornum > _disk_num_sectors)
    {
        Debug_printf("::read sector %d > %lu\r\n", sectornum, _disk_num_sectors);
        return true;
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

    return err;
}

bool inHighScoreRange(int minimum, int maximum, int val)
{
    return ((minimum <= val) && (val <= maximum));
}

// Returns TRUE if an error condition occurred
bool MediaTypeATR::write(uint16_t sectornum, bool verify)
{
    fnFile *oldFileh, *hsFileh;

    oldFileh = nullptr;
    hsFileh = nullptr;

    Debug_printf("ATR WRITE %d / %lu\r\n", sectornum, _disk_num_sectors);

    // Return an error if we're trying to write beyond the end of the disk
    if (sectornum > _disk_num_sectors)
    {
        Debug_printf("::write sector %d > %lu\r\n", sectornum, _disk_num_sectors);
        return true;
    }

    if (_high_score_sector != 0)
    {
        Debug_printf("High score mode activated, attempting write open\r\n");
        if (_disk_host == nullptr)
        {
            Debug_printf("!!! Why is host slot null?\r\n");
        }
        else
        {
            oldFileh = _disk_fileh;
            hsFileh = _disk_host->fnfile_open(_disk_filename, _disk_filename, strlen(_disk_filename) + 1, "rb+");
            _disk_fileh = hsFileh;
        }
    }
    uint16_t sectorSize = sector_size(sectornum);
    uint32_t offset = _sector_to_offset(sectornum);

    _disk_last_sector = INVALID_SECTOR_VALUE;

    // Perform a seek if we're writing to the sector after the last one
    int e;
    if (sectornum != _disk_last_sector + 1)
    {
        e = fnio::fseek(_disk_fileh, offset, SEEK_SET);
        if (e != 0)
        {
            Debug_printf("::write seek error %d\r\n", e);
            return true;
        }
    }
    // Write the data
    e = fnio::fwrite(_disk_sectorbuff, 1, sectorSize, _disk_fileh);
    if (e != sectorSize)
    {
        Debug_printf("::write error %d, %d\r\n", e, errno);
        return true;
    }

    int ret = fnio::fflush(_disk_fileh); // Since we might get reset at any moment, go ahead and sync the file
    Debug_printf("ATR::write fflush:%d\r\n", ret);

    if (_high_score_sector != 0)
    {
        Debug_printf("Closing high score sector.\r\n");

        if (hsFileh != nullptr)
            fnio::fclose(hsFileh);

        _disk_fileh = oldFileh;
        _disk_last_sector = INVALID_SECTOR_VALUE; // force a cache invalidate.
    }
    else
        _disk_last_sector = sectornum;

    return false;
}

void MediaTypeATR::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;
    else if (_disk_sector_size > 128)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_SIDED;



    statusbuff[1] = ~_disk_controller_status; // Negate the controller status
}

/*
    From Altirra manual:
    The format command formats a disk, writing 40 tracks and then verifying all sectors.
    All sectors are filleded with the data byte $00. On completion, the drive returns
    a sector-sized buffer containing a list of 16-bit bad sector numbers terminated by $FFFF.
*/
// Returns TRUE if an error condition occurred
bool MediaTypeATR::format(uint16_t *responsesize)
{
    Debug_print("ATR FORMAT\r\n");

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
mediatype_t MediaTypeATR::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("ATR MOUNT\r\n");

    _disktype = MEDIATYPE_UNKNOWN;

    uint16_t num_bytes_sector;
    uint32_t num_paragraphs;
    uint8_t buf[16];

    // Get file and sector size from header
    int i;
    if ((i = fnio::fseek(f, 0, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to header on disk image (%d, %d)\r\n", i, errno);
        return _disktype;
    }
    if ((i = fnio::fread(buf, 1, sizeof(buf), f)) != sizeof(buf))
    {
        Debug_printf("failed reading header bytes (%d, %d)\r\n", i, errno);
        return _disktype;
    }
    // Check the magic number
    if (UINT16_FROM_HILOBYTES(buf[1], buf[0]) != ATR_MAGIC_HEADER)
    {
        Debug_println("ATR header missing 'NICKATARI'");
        return _disktype;
    }

    num_bytes_sector = UINT16_FROM_HILOBYTES(buf[5], buf[4]);

    num_paragraphs = UINT16_FROM_HILOBYTES(buf[3], buf[2]);
    num_paragraphs = num_paragraphs | (buf[6] << 16);

    _disk_sector_size = num_bytes_sector;

    _disk_num_sectors = (num_paragraphs * 16) / num_bytes_sector;
    // Adjust sector size for the fact that the first three sectors are *always* 128 bytes
    if (num_bytes_sector == 256)
        _disk_num_sectors += 2;

    derive_percom_block(_disk_num_sectors);

    _disk_fileh = f;
    _disk_image_size = disksize;
    _disk_last_sector = INVALID_SECTOR_VALUE;

    _high_score_sector = UINT16_FROM_HILOBYTES(buf[14], buf[13]);
    _high_score_num_sectors = buf[12] - 1;

    if (_high_score_sector > 0)
        Debug_printf("High Score Sector Specified: %u\r\n", _high_score_sector);

    Debug_printf("mounted ATR: paragraphs=%lu, sect_size=%d, sect_count=%lu, disk_size=%lu\r\n",
                 num_paragraphs, num_bytes_sector, _disk_num_sectors, disksize);

    _disktype = MEDIATYPE_ATR;

    return _disktype;
}

// Returns FALSE on error
bool MediaTypeATR::create(fnFile *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("ATR CREATE\r\n");

    struct
    {
        uint8_t magicL;
        uint8_t magicH;
        uint8_t filesizeL;
        uint8_t filesizeH;
        uint8_t secsizeL;
        uint8_t secsizeH;
        uint8_t filesizeHH;
        uint8_t res0;
        uint8_t res1;
        uint8_t res2;
        uint8_t res3;
        uint8_t res4;
        uint8_t res5;
        uint8_t res6;
        uint8_t res7;
        uint8_t flags;
    } atrHeader;

    memset(&atrHeader, 0, sizeof(atrHeader));

    uint32_t total_size = numSectors * sectorSize;
    // Adjust for first 3 sectors always being single-density (we lose 384 bytes)
    // we don't do this for 512 byte sectors
    if (sectorSize == 256)
        total_size -= 384; // 3 * 128

    uint32_t num_paragraphs = total_size / 16;

    // Write header
    atrHeader.magicL = LOBYTE_FROM_UINT16(ATR_MAGIC_HEADER);
    atrHeader.magicH = HIBYTE_FROM_UINT16(ATR_MAGIC_HEADER);

    atrHeader.filesizeL = LOBYTE_FROM_UINT16(num_paragraphs);
    atrHeader.filesizeH = HIBYTE_FROM_UINT16(num_paragraphs);
    atrHeader.filesizeHH = (num_paragraphs & 0xFF0000) >> 16;

    atrHeader.secsizeL = LOBYTE_FROM_UINT16(sectorSize);
    atrHeader.secsizeH = HIBYTE_FROM_UINT16(sectorSize);

    Debug_printf("Write header to ATR: sec_size=%d, sectors=%d, paragraphs=%lu, bytes=%lu\r\n",
                 sectorSize, numSectors, num_paragraphs, total_size);

    uint32_t offset = fnio::fwrite(&atrHeader, 1, sizeof(atrHeader), f);

    // Write first three 128 uint8_t sectors
    uint8_t blank[512] = {0};

    if (sectorSize < 512)
    {
        for (int i = 0; i < 3; i++)
        {
            size_t out = fnio::fwrite(blank, 1, 128, f);
            if (out != 128)
            {
                Debug_printf("Error writing sector %d\r\n", i);
                return false;
            }
            offset += 128;
            numSectors--;
        }
    }

    // Write the rest of the sectors via sparse seek to the last sector
    offset += (numSectors * sectorSize) - sectorSize;
    fnio::fseek(f, offset, SEEK_SET);
    size_t out = fnio::fwrite(blank, 1, sectorSize, f);

    if (out != sectorSize)
    {
        Debug_println("Error writing last sector");
        return false;
    }

    return true;
}
#endif /* BUILD_ATARI */
