#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "disk.h"

#include "diskTypeAtx.h"

#define ATX_MAGIC_HEADER 0x41543858 // "AT8X"

// Returns byte offset of given sector number (1-based)
uint32_t DiskTypeATX::_sector_to_offset(uint16_t sectorNum)
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
uint16_t DiskTypeATX::sector_size(uint16_t sectornum)
{
    return sectornum <= 3 ? 128 : _sectorSize;
}

// Returns TRUE if an error condition occurred
bool DiskTypeATX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("ATX READ\n");

    *readcount = 0;

    // Return an error if we're trying to read beyond the end of the disk
    if(sectornum > _numSectors)
    {
        Debug_printf("::read sector %d > %d\n", sectornum, _numSectors);        
        return true;
    }

    uint16_t sectorSize = sector_size(sectornum);

    memset(_sectorbuff, 0, sizeof(_sectorbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _lastSectorUsed + 1)
    {
        uint32_t offset = _sector_to_offset(sectornum);
        err = fseek(_file, offset, SEEK_SET) != 0;
    }

    if (err == false)
        err = fread(_sectorbuff, 1, sectorSize, _file) != sectorSize;

    if (err == false)
        _lastSectorUsed = sectornum;
    else
        _lastSectorUsed = INVALID_SECTOR_VALUE;

    *readcount = sectorSize;

    return err;
}

// ATX disks do not support WRITE
bool DiskTypeATX::write(uint16_t sectornum, bool verify)
{
    Debug_print("ATX WRITE (not allowed)\n");
    return true;
}

void DiskTypeATX::status(uint8_t statusbuff[4])
{
    if (_sectorSize == 256)
        statusbuff[0] |= 0x20; // XF551 double-density bit

    if (_percomBlock.num_sides == 1)
        statusbuff[0] |= 0x40; // XF551 double-sided bit

    if (_percomBlock.sectors_per_trackL == 26)
        statusbuff[0] |= 0x80; // 1050 enhanced-density bit
}

// ATX disks do not support FORMAT
bool DiskTypeATX::format(uint16_t *responsesize)
{
    Debug_print("ATX FORMAT (not allowed)\n");
    return true;
}

/*
 Load the data records that make up the ATX image into memory
 Returns FALSE on failure
*/
bool DiskTypeATX::_load_atx_data(FILE *f, atxheader *hdr)
{
    Debug_println("DiskTypeATX::_load_atx_data starting read");

    // Seek to the start of the ATX record data
    int i;
    if ((i = fseek(f, hdr->start, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to start of ATX data (%d, %d)\n", i, errno);
        return false;
    }


    return false;
}

/* 
 Mount ATX disk
 Header layout details from:
 http://a8preservation.com/#/guides/atx

 Since timing is important, we will load the entire image into memory.
 
*/
disktype_t DiskTypeATX::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ATX MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;

    // Load the first 36 bytes of the file to examine the header before attempting to load the rest
    int i;
    if ((i = fseek(f, 0, SEEK_SET)) < 0)
    {
        Debug_printf("failed seeking to header on disk image (%d, %d)\n", i, errno);
        return _disktype;
    }

    atxheader hdr;

    if ((i = fread(&hdr, 1, sizeof(hdr), f)) != sizeof(hdr))
    {
        Debug_printf("failed reading header bytes (%d, %d)\n", i, errno);
        return _disktype;
    }

    // Check the magic number (flip it around since it automatically gets re-ordered when loaded as a UINT32)
    if (ATX_MAGIC_HEADER != UINT32_FROM_LE_UINT32(hdr.magic))
    {
        Debug_printf("ATX header doesnt match 'AT8X' (0x%008x)\n", hdr.magic);
        return _disktype;
    }

    Debug_print("ATX image header values:\n");
    Debug_printf("version: %hd, version min: %hd\n", hdr.version, hdr.min_version);
    Debug_printf("creator: 0x%02x, creator ver: %hd\n", hdr.creator, hdr.creator_version);
    Debug_printf("  flags: 0x%02x\n", hdr.flags);
    Debug_printf("   type: %hu, density: %hu\n", hdr.image_type, hdr.density);
    Debug_printf("imageid: 0x%02x, image ver: %hd\n", hdr.image_id, hdr.image_version);
    Debug_printf("  start: 0x%04x\n", hdr.start);
    Debug_printf("    end: 0x%04x\n", hdr.end);

    // Load all the actual ATX records into memory (return immediately if we fail)
    if(_load_atx_data(f, &hdr) == false)
        return _disktype;

    _file = f;
    _lastSectorUsed = INVALID_SECTOR_VALUE;

    _disktype = DISKTYPE_ATX;

    return _disktype;
}

// ATX creation not allowed
bool DiskTypeATX::create(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("ATX CREATE (not allowed)\n");
    return false;
}
