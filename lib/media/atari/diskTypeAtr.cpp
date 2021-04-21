#include "diskTypeAtr.h"

#include <memory.h>
#include <string.h>

#include "../../../include/debug.h"

#include "utils.h"
#include "../../bus/sio/disk.h"


#define ATR_MAGIC_HEADER 0x0296 // Sum of 'NICKATARI'

// Returns byte offset of given sector number (1-based)
uint32_t DiskTypeATR::_sector_to_offset(uint16_t sectorNum)
{
    uint32_t offset = 0;

    switch (sectorNum)
    {
        case 1:
            offset=16;
            break;
        case 2:
            offset=144;
            break;
        case 3:
            offset=272;
            break;
        default:
            if (_disk_sector_size == 256)
                offset=((sectorNum-3)*256)+16+128;
            else
                offset=((sectorNum-1)*128)+16;
            
            break;
    }

    return offset;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATR::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("ATR READ\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if(sectornum > _disk_num_sectors)
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
bool DiskTypeATR::write(uint16_t sectornum, bool verify)
{
    Debug_printf("ATR WRITE\n", sectornum, _disk_num_sectors);

    // Return an error if we're trying to write beyond the end of the disk
    if(sectornum > _disk_num_sectors)
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
    Debug_printf("ATR::write fsync:%d\n", ret);

    _disk_last_sector = sectornum;

    return false;
}

void DiskTypeATR::status(uint8_t statusbuff[4])
{
    statusbuff[0] = DISK_DRIVE_STATUS_CLEAR;

    if (_disk_sector_size == 256)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_SIDED;

    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= DISK_DRIVE_STATUS_ENHANCED_DENSITY;

    statusbuff[1] = ~ _disk_controller_status; // Negate the controller status
}

/*
    From Altirra manual:
    The format command formats a disk, writing 40 tracks and then verifying all sectors.
    All sectors are filleded with the data byte $00. On completion, the drive returns
    a sector-sized buffer containing a list of 16-bit bad sector numbers terminated by $FFFF.
*/
// Returns TRUE if an error condition occurred
bool DiskTypeATR::format(uint16_t *responsesize)
{
    Debug_print("ATR FORMAT\n");

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
disktype_t DiskTypeATR::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ATR MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;

    uint16_t num_bytes_sector;
    uint32_t num_paragraphs;
    uint8_t buf[7];

    // Get file and sector size from header
    int i;
    if ((i = fseek(f, 0, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to header on disk image (%d, %d)\n", i, errno);
        return _disktype;
    }
    if ((i = fread(buf, 1, sizeof(buf), f)) != sizeof(buf))
    {
        Debug_printf("failed reading header bytes (%d, %d)\n", i, errno);
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

    Debug_printf("mounted ATR: paragraphs=%d, sect_size=%d, sect_count=%d, disk_size=%d\n",
                 num_paragraphs, num_bytes_sector, _disk_num_sectors, disksize);

    _disktype = DISKTYPE_ATR;

    return _disktype;
}

// Returns FALSE on error
bool DiskTypeATR::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("ATR CREATE\n");

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
    if (sectorSize > 128)
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

    Debug_printf("Write header to ATR: sec_size=%d, sectors=%d, paragraphs=%d, bytes=%d\n",
        sectorSize, numSectors, num_paragraphs, total_size);

    uint32_t offset = fwrite(&atrHeader, 1, sizeof(atrHeader), f);

    // Write first three 128 uint8_t sectors
    uint8_t blank[256] = {0};

    for (int i = 0; i < 3; i++)
    {
        size_t out = fwrite(blank, 1, 128, f);
        if (out != 128)
        {
            Debug_printf("Error writing sector %hhu\n", i);
            return false;
        }
        offset += 128;
        numSectors--;
    }

    // Write the rest of the sectors via sparse seek to the last sector
    offset += (numSectors * sectorSize) - sectorSize;
    fseek(f, offset, SEEK_SET);
    size_t out = fwrite(blank, 1, sectorSize, f);

    if (out != sectorSize)
    {
        Debug_println("Error writing last sector");
        return false;
    }

    return true;
}
