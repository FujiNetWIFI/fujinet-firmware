#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "disk.h"

#include "diskTypeAtr.h"

#define ATR_MAGIC_HEADER 0x0296 // Sum of 'NICKATARI'

// Returns byte offset of given sector number (1-based)
uint32_t DiskTypeATR::_sector_to_offset(uint16_t sectorNum)
{
    uint32_t offset = 0;

    // This should always be true, but just so we don't end up with a negative...
    if (sectorNum > 0)
        offset = _sectorSize * (sectorNum - 1);

    offset += 16; // Adjust for ATR header

    // Adjust for the fact that the first 3 sectors are always 128-bytes even on 256-byte disks
    if (_sectorSize == 256 && sectorNum > 3)
        offset -= 384;

    return offset;
}

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t DiskTypeATR::sector_size(uint16_t sectornum)
{
    return sectornum <= 3 ? 128 : _sectorSize;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATR::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("ATR READ\n");

    *readcount = 0;

    uint16_t sectorSize = sector_size(sectornum);

    memset(_sectorbuff, 0, sizeof(_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _lastSectorNum + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fseek(_file, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fread(_sectorbuff, 1, sectorSize, _file) != sectorSize;

    if (err == false)
        _lastSectorNum = sectornum;
    else
        _lastSectorNum = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    return err;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATR::write(uint16_t sectornum, bool verify)
{
    Debug_print("ATR WRITE\n");

    uint16_t sectorSize = sector_size(sectornum);
    uint32_t offset = _sector_to_offset(sectornum);

    _lastSectorNum = INVALID_SECTOR_VALUE;

    // Perform a seek if we're writing to the sector after the last one
    if (sectornum != _lastSectorNum + 1)
    {
        if (fseek(_file, offset, SEEK_SET) != 0)
            return true;
    }

    if (fwrite(_sectorbuff, 1, sectorSize, _file) != sectorSize)
    {
        return true;
    }

    int ret = fflush(_file);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_file)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("ATR::write fsync:%d\n", ret);

    _lastSectorNum = sectornum;

    return false;
}

void DiskTypeATR::status(uint8_t statusbuff[4])
{

    // TODO: bit 6 for double-sided
    if (_sectorSize == 256)
        statusbuff[0] |= 0x20;

    // TODO: bit 7 should be set whenever we have 26 sectors per track (1050 Enahanced)
    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= 0x80;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATR::format(uint16_t *responsesize)
{
    Debug_print("ATR FORMAT\n");

    // Populate bad sector map (no bad sectors)
    memset(_sectorbuff, 0, sizeof(_sectorbuff));
    _sectorbuff[0] = 0xFF; // no bad sectors.
    _sectorbuff[1] = 0xFF;

    *responsesize = _sectorSize;

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
disktype_t DiskTypeATR::mount(FILE *f)
{
    Debug_print("ATR MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;

    uint16_t num_bytes_sector;
    uint32_t num_paragraphs;
    uint16_t num_sectors;
    uint8_t buf[7];

    // Get file and sector size from header

    if (fseek(f, 0, SEEK_SET) < 0)
    {
        Debug_println("failed seeking to header on disk image");
        return _disktype;
    }
    int i;
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

    _sectorSize = num_bytes_sector;

    num_sectors = (num_paragraphs * 16) / num_bytes_sector;
    // Adjust sector size for the fact that the first three sectors are *always* 128 bytes
    if (num_bytes_sector == 256)
        num_sectors += 2;

    derive_percom_block(num_sectors);

    _file = f;
    _lastSectorNum = INVALID_SECTOR_VALUE;

    Debug_printf("mounted ATR: paragraphs=%hu, sect_size=%hu, sect_count=%hu\n",
                 num_paragraphs, num_bytes_sector, num_sectors);

    _disktype = DISKTYPE_ATR;

    return _disktype;
}

// Returns FALSE on error
bool DiskTypeATR::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("ATR CREATE\n");

    union {
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
        };
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

    Debug_printf("Write header to ATR: sec_size=%hu, sectors=%hu, paragraphs=%hu\n");

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

    // Write the rest of the sectors via sparse seek
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
