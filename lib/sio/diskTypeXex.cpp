#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "disk.h"

#include "diskTypeXex.h"

#define BOOTLOADER "/picoboot.bin"

#define BOOTLOADER_END 0x03

#define DIRECTORY_START 0x0169
#define DIRECTORY_END 0x0170

#define FIRST_XEX_SECTOR 0x04

#define SECTOR_LINK_SIZE 3 

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t DiskTypeXEX::sector_size(uint16_t sectornum)
{
    return _sectorSize;
}

/*
    The bootloader expects to find a file named "AUTORUN", so fake a directory
    with only that file.
    00 : Flags:
        $00 - Entry has never been used
        $01 - File opened for output
        $02 - File created by DOS 2
        $20 - Entry locked
        $40 - Entry in use
        $80 - Entry has been deleted
    01 : Size in sectors, low
    02 : Size in sectors, high
    03 : Start sector, low
    04 : Start sector, high
    05-12: Filename
    13-15: Extension
*/
void DiskTypeXEX::_fake_directory_entry()
{
    // Calculate the number of sectors required
    uint16_t data_per_sector = _sectorSize - SECTOR_LINK_SIZE;
    uint16_t numsectors = _disksize / data_per_sector;
    numsectors += _disksize % data_per_sector > 0 ? 1 : 0;

    Debug_printf("num XEX sectors = %d\n", numsectors);

    _sectorbuff[0] = 0x42; // Created by DOS 2 and in use

    _sectorbuff[1] = LOBYTE_FROM_UINT16(numsectors);
    _sectorbuff[2] = HIBYTE_FROM_UINT16(numsectors);

    _sectorbuff[3] = LOBYTE_FROM_UINT16(FIRST_XEX_SECTOR);
    _sectorbuff[4] = HIBYTE_FROM_UINT16(FIRST_XEX_SECTOR);

    _sectorbuff[5] = 'A';
    _sectorbuff[6] = 'U';
    _sectorbuff[7] = 'T';
    _sectorbuff[8] = 'O';
    _sectorbuff[9] = 'R';
    _sectorbuff[10] = 'U';
    _sectorbuff[11] = 'N';
    _sectorbuff[12] = 0x20;
    _sectorbuff[13] = 0x20;
    _sectorbuff[14] = 0x20;
    _sectorbuff[15] = 0x20;
}

// Returns TRUE if an error condition occurred
bool DiskTypeXEX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("XEX READ\n");

    *readcount = _sectorSize;

    memset(_sectorbuff, 0, sizeof(_sectorbuff));

    bool err = false;

    // Load from our bootloader first
    if (sectornum <= BOOTLOADER_END)
    {
        int offset = _sectorSize * (sectornum - 1);
        int remain = _bootloadersize - offset;
        int bootcopy = _sectorSize > remain ? remain : _sectorSize;

        Debug_printf("copying %d bytes from bootloader\n", bootcopy);

        memcpy(_sectorbuff, _bootloader + offset, bootcopy);

        // Note that we may not have read an entire sector's worth of bytes. That's okay.
        _lastSectorNum = INVALID_SECTOR_VALUE; // Reset this so we're forced to seek
        return false;
    }

    // We're going to fake a DOS2.0 directory if we're seeking to the directory area
    if (sectornum >= DIRECTORY_START && sectornum <= DIRECTORY_END)
    {
        Debug_print("faking DOS 2 directory\n");
        _fake_directory_entry();
        _lastSectorNum = INVALID_SECTOR_VALUE; // Reset this so we're forced to seek        
        return false;
    }

    int data_bytes = _sectorSize - SECTOR_LINK_SIZE;
    // This is the number of bytes into the XEX file we should be reading
    int xex_offset = data_bytes * (sectornum - FIRST_XEX_SECTOR);

    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _lastSectorNum + 1)
    {
        Debug_printf("seeking to offset %d in XEX\n", xex_offset);
        err = fseek(_file, xex_offset, SEEK_SET) != 0;
    }

    if (err == false)
    {
        Debug_printf("requesting %d bytes from XEX\n", data_bytes);
        int read = fread(_sectorbuff, 1, data_bytes, _file);
        Debug_printf("received %d bytes\n", read);

        // Fill in the sector link data pointing to the next sector
        if(read >= 0)
        {
            // Provide number of bytes read
            _sectorbuff[_sectorSize - 1] = read;

            // Only provide a next sector pointer if we read a full sector of data
            if(read == data_bytes)
            {
                uint16_t next_sector = sectornum + 1;
                _sectorbuff[_sectorSize - 2] = LOBYTE_FROM_UINT16(next_sector);
                _sectorbuff[_sectorSize - 3] = HIBYTE_FROM_UINT16(next_sector);
            }
        }
        else
            err = true;
    }

    if (err == false)
        _lastSectorNum = sectornum;
    else
        _lastSectorNum = INVALID_SECTOR_VALUE;

    return err;
}

void DiskTypeXEX::status(uint8_t statusbuff[4])
{
    // TODO: Set double density, etc. if needed
}

void DiskTypeXEX::unmount()
{
    if (_bootloader != nullptr)
        free(_bootloader);

    // Call the parent unmount
    this->DiskType::unmount();
}

DiskTypeXEX::~DiskTypeXEX()
{
    unmount();
}

disktype_t DiskTypeXEX::mount(FILE *f, uint32_t disksize)
{
    Debug_print("XEX MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;

    // Load our bootloader
    _bootloadersize = fnSystem.load_firmware(BOOTLOADER, &_bootloader);
    if (_bootloadersize < 0)
    {
        Debug_printf("failed to load bootloader \"%s\"\n", BOOTLOADER);
        return _disktype;
    }

    _sectorSize = 128;
    _file = f;
    _disksize = disksize;
    _lastSectorNum = INVALID_SECTOR_VALUE;
    _disktype = DISKTYPE_XEX;

    Debug_printf("mounted XEX with %d-byte bootloader; XEX size=%d\n", _bootloadersize, _disksize);

    return _disktype;
}
