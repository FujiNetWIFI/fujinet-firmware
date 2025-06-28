#ifdef BUILD_ATARI

#include "diskTypeXex.h"

#include <cstring>
#include <stdlib.h>
#include <memory.h>

#include "../../include/debug.h"

#include "fnSystem.h"

#include "utils.h"


#define BOOTLOADER "/picoboot.bin"

#define BOOTLOADER_END 0x03

// Sector 0x168 (360) contains the sector table
// Sector 0x169 (361) is the start of the file table of contents
// Normally the table of contents continues to sector 0x182 (368)
#define VTOC_SECTOR 0x168
#define DIRECTORY_START 0x0169
#define DIRECTORY_END 0x0170

#define FIRST_XEX_SECTOR 0x0171

#define SECTOR_SIZE 256
#define BOOT_SECTOR_SIZE 128

#define SECTOR_LINK_SIZE 3

/*
    The bootloader expects to find a file named "AUTORUN", so fake a directory
    with only that file.
    00 : Flags:
        $00 - Entry has never been used
        $01 - File opened for output
        $02 - File created by DOS 2
        $04 - Use 16-bit sector links ** (PICOBOOT) **
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

void MediaTypeXEX::_fake_vtoc()
{
    uint16_t data_per_sector = _disk_sector_size - SECTOR_LINK_SIZE;
    uint16_t numsectors = _disk_image_size / data_per_sector;
    numsectors += _disk_image_size % data_per_sector > 0 ? 1 : 0;

    uint16_t freesectors = 0x2D0 - numsectors;

    Debug_printf("num XEX sectors = %d\r\n", numsectors);

    memset(_disk_sectorbuff,0,sizeof(_disk_sectorbuff));

    _disk_sectorbuff[0] = 0x02;
    _disk_sectorbuff[1] = 0xd0;
    _disk_sectorbuff[2] = 0x02;
    _disk_sectorbuff[3] = freesectors & 0xFF;
    _disk_sectorbuff[4] = freesectors << 8;

    // Fill VTOC
    for (int i=10; i<100; i++)
    {
        unsigned char b=0;

        for (int j=0; j<8; j++)
        {
            if (numsectors--)
                b |= 1;
            else
                b |= 0;
            b <<= 1;
        }
        _disk_sectorbuff[i] = b;
    }
}

void MediaTypeXEX::_fake_directory_entry()
{
    // Calculate the number of sectors required for XEX file
    uint16_t data_per_sector = _disk_sector_size - SECTOR_LINK_SIZE;
    uint16_t numsectors = _disk_image_size / data_per_sector;
    numsectors += _disk_image_size % data_per_sector > 0 ? 1 : 0;

    Debug_printf("num XEX sectors = %d\r\n", numsectors);

    _disk_sectorbuff[0] = 0x46; // Entry in use; 16-bit sector links; created by DOS 2

    _disk_sectorbuff[1] = LOBYTE_FROM_UINT16(numsectors);
    _disk_sectorbuff[2] = HIBYTE_FROM_UINT16(numsectors);

    _disk_sectorbuff[3] = LOBYTE_FROM_UINT16(FIRST_XEX_SECTOR);
    _disk_sectorbuff[4] = HIBYTE_FROM_UINT16(FIRST_XEX_SECTOR);

    _disk_sectorbuff[5] = 'A';
    _disk_sectorbuff[6] = 'U';
    _disk_sectorbuff[7] = 'T';
    _disk_sectorbuff[8] = 'O';
    _disk_sectorbuff[9] = 'R';
    _disk_sectorbuff[10] = 'U';
    _disk_sectorbuff[11] = 'N';
    _disk_sectorbuff[12] = 0x20;
    _disk_sectorbuff[13] = 0x20;
    _disk_sectorbuff[14] = 0x20;
    _disk_sectorbuff[15] = 0x20;
}

// Returns TRUE if an error condition occurred
bool MediaTypeXEX::read(uint16_t sectornum, uint16_t *readcount)
{
    Debug_printf("XEX READ (%d)\r\n", sectornum);


    memset(_disk_sectorbuff, 0, sizeof(_disk_sectorbuff));

    bool err = false;

    // Load from our bootloader first
    if (sectornum <= BOOTLOADER_END)
    {
        int offset = BOOT_SECTOR_SIZE * (sectornum - 1);
        int remain = _xex_bootloadersize - offset;
        int bootcopy = BOOT_SECTOR_SIZE > remain ? remain : BOOT_SECTOR_SIZE;

        *readcount = BOOT_SECTOR_SIZE;

        Debug_printf("copying %d bytes from bootloader\r\n", bootcopy);
        memcpy(_disk_sectorbuff, _xex_bootloader + offset, bootcopy);

        // PicoBoot uses the first byte as a flag for whether it should read double or single density sectors
        // Single = 0x80, Double = 0x00
        if(SECTOR_SIZE == 256 && sectornum == 1 && _disk_sectorbuff[0] == 0x80)
        {
            Debug_print("setting PicoBoot double density flag\r\n");
            _disk_sectorbuff[0] = 0x00;
        }

        // Note that we may not have read an entire sector's worth of bytes. That's okay.
        _disk_last_sector = INVALID_SECTOR_VALUE; // Reset this so we're forced to seek
        return false;
    }

    *readcount = _disk_sector_size;

    // We're going to fake a DOS2.0 directory if we're seeking to the directory area
    if (sectornum == VTOC_SECTOR)
    {
        Debug_printf("faking DOS 2 VTOC\r\n");
        _fake_vtoc();
        _disk_last_sector = INVALID_SECTOR_VALUE;
        return false;
    }
    else if (sectornum >= DIRECTORY_START && sectornum <= DIRECTORY_END)
    {
        Debug_print("faking DOS 2 directory\r\n");
        _fake_directory_entry();
        _disk_last_sector = INVALID_SECTOR_VALUE; // Reset this so we're forced to seek        
        return false;
    }

    int data_bytes = _disk_sector_size - SECTOR_LINK_SIZE;
    // This is the number of bytes into the XEX file we should be reading
    int xex_offset = data_bytes * (sectornum - FIRST_XEX_SECTOR);

    // Perform a seek if we're not reading the sector after the last one we read
    if (sectornum != _disk_last_sector + 1)
    {
        Debug_printf("seeking to offset %d in XEX\r\n", xex_offset);
        err = fnio::fseek(_disk_fileh, xex_offset, SEEK_SET) != 0;
    }

    if (err == false)
    {
        Debug_printf("requesting %d bytes from XEX\r\n", data_bytes);
        int read = fnio::fread(_disk_sectorbuff, 1, data_bytes, _disk_fileh);
        Debug_printf("received %d bytes\r\n", read);

        // Fill in the sector link data pointing to the next sector
        if(read >= 0)
        {
            // Provide number of bytes read
            _disk_sectorbuff[_disk_sector_size - 1] = read;

            // Only provide a next sector pointer if we read a full sector of data
            if(read == data_bytes)
            {
                uint16_t next_sector = sectornum + 1;
                _disk_sectorbuff[_disk_sector_size - 2] = LOBYTE_FROM_UINT16(next_sector);
                _disk_sectorbuff[_disk_sector_size - 3] = HIBYTE_FROM_UINT16(next_sector);
            }
        }
        else
            err = true;
    }

    if (err == false)
        _disk_last_sector = sectornum;
    else
        _disk_last_sector = INVALID_SECTOR_VALUE;

    return err;
}

void MediaTypeXEX::status(uint8_t statusbuff[4])
{
    statusbuff[0] |= DISK_DRIVE_STATUS_DOUBLE_DENSITY;
}

void MediaTypeXEX::unmount()
{
    // Call the parent unmount
    this->MediaType::unmount();
}

MediaTypeXEX::~MediaTypeXEX()
{
    unmount();
}

mediatype_t MediaTypeXEX::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("XEX MOUNT\r\n");

    _disktype = MEDIATYPE_UNKNOWN;

    // Load our bootloader
    _xex_bootloadersize = fnSystem.load_firmware(BOOTLOADER, &_xex_bootloader[0]);
    if (_xex_bootloadersize < 0)
    {
        Debug_printf("failed to load bootloader \"%s\"\r\n", BOOTLOADER);
        return _disktype;
    }

    _disk_sector_size = SECTOR_SIZE;
    _disk_fileh = f;
    _disk_image_size = disksize;
    _disk_last_sector = INVALID_SECTOR_VALUE;
    _disktype = MEDIATYPE_XEX;

    // Calculate the number of fake disk sectors
    uint16_t data_per_sector = _disk_sector_size - SECTOR_LINK_SIZE;
    _disk_num_sectors = FIRST_XEX_SECTOR + _disk_image_size / data_per_sector;
    _disk_num_sectors += _disk_image_size % data_per_sector > 0 ? 1 : 0;

    if (_disk_num_sectors < 720)
        _disk_num_sectors = 720;

    Debug_printf("mounted XEX with %d-byte bootloader; XEX size=%lu\r\n", _xex_bootloadersize, _disk_image_size);
    Debug_printf("disk sectors = %lu\r\n", _disk_num_sectors);

    return _disktype;
}

#endif /* BUILD_ATARI */
