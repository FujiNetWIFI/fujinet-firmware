#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "disk.h"

#include "diskTypeXex.h"

#define BOOTLOADER_HISPEED "/autoboot-hispeed.bin"
#define BOOTLOADER "/autoboot.bin"

// Returns sector size taking into account that the first 3 sectors are always 128-byte
// SectorNum is 1-based
uint16_t DiskTypeXEX::sector_size(uint16_t sectornum)
{
    return _sectorSize;
}

// Returns TRUE if an error condition occurred
bool DiskTypeXEX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_print("XEX READ\n");

    *readcount = _sectorSize;

    uint32_t offset = _sectorSize * (sectornum - 1);

    memset(_sectorbuff, 0, sizeof(_sectorbuff));

    bool err = false;
    int bootcopy = 0;

    // Load from our bootloader first
    if (offset < _bootloadersize)
    {
        int remain = _bootloadersize - offset;
        bootcopy = _sectorSize > remain ? remain : _sectorSize;

        Debug_printf("copying %d bytes from bootloader\n", bootcopy);

        memcpy(_sectorbuff, _bootloader, bootcopy);


        // Return if we've read an entire sector's worth of bytes
        if(bootcopy >= _sectorSize)
        {
            _lastSectorNum = sectornum;
            return false;
        }
    }

    // Adjust for the bootloader size
    offset -= _bootloadersize;
    // Don't copy a full sector if we've already copied some from the bootloader
    int copysize = _sectorSize - bootcopy;

    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _lastSectorNum + 1)
    {
        Debug_printf("seeking to offset %d in XEX\n", offset);
        err = fseek(_file, offset, SEEK_SET) != 0;
    }

    if (err == false)
    {
        Debug_printf("reading %d bytes from XEX\n", copysize);
        err = fread(_sectorbuff + bootcopy, 1, copysize, _file) != copysize;
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
    if(_bootloader != nullptr)
        free(_bootloader);

    // Call the parent unmount
    this->DiskType::unmount();
}

DiskTypeXEX::~DiskTypeXEX()
{
    unmount();
}

disktype_t DiskTypeXEX::mount(FILE *f)
{
    Debug_print("XEX MOUNT\n");

    _disktype = DISKTYPE_UNKNOWN;

    // Load our bootloader
    _bootloadersize = fnSystem.load_firmware(BOOTLOADER_HISPEED, &_bootloader);
    if(_bootloadersize < 0)
    {
        Debug_printf("failed to load bootloader \"%s\"\n", BOOTLOADER_HISPEED);
        return _disktype;
    }

    _sectorSize = 128;
    _file = f;
    _lastSectorNum = INVALID_SECTOR_VALUE;
    _disktype = DISKTYPE_XEX;

    Debug_printf("mounted XEX with %d-byte bootloader\n", _bootloadersize);

    return _disktype;
}
