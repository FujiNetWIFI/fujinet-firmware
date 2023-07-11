#ifdef BUILD_CX16

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "fuji.h"
#include "utils.h"

#define SIO_DISKCMD_FORMAT 0x21
#define SIO_DISKCMD_FORMAT_MEDIUM 0x22
#define SIO_DISKCMD_PUT 0x50
#define SIO_DISKCMD_READ 0x52
#define SIO_DISKCMD_STATUS 0x53
#define SIO_DISKCMD_WRITE 0x57

#define SIO_DISKCMD_HSIO_INDEX 0x3F
#define SIO_DISKCMD_HSIO_FORMAT 0xA1
#define SIO_DISKCMD_HSIO_FORMAT_MEDIUM 0xA2
#define SIO_DISKCMD_HSIO_PUT 0xD0
#define SIO_DISKCMD_HSIO_READ 0xD2
#define SIO_DISKCMD_HSIO_STATUS 0xD3
#define SIO_DISKCMD_HSIO_WRITE 0xD7

#define SIO_DISKCMD_PERCOM_READ 0x4E
#define SIO_DISKCMD_PERCOM_WRITE 0x4F

// External ref to fuji object.
extern cx16Fuji theFuji;

cx16Disk::cx16Disk()
{
    device_active = false;
}

// Read disk data and send to computer
void cx16Disk::sio_read()
{
    // Debug_print("disk READ\n");

    if (_disk == nullptr)
    {
        cx16_error();
        return;
    }

    uint16_t readcount;

    bool err = _disk->read(UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1), &readcount);

    // Send result to Atari
    bus_to_computer(_disk->_media_blockbuff, readcount, err);
}

// Write disk data from computer
void cx16Disk::sio_write(bool verify)
{
    // TODO IMPLEMENT
}

// Status
void cx16Disk::status()
{
    // TODO IMPLEMENT
}

// Disk format
void cx16Disk::sio_format()
{
    // TODO IMPLEMENT
}

/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extension or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t cx16Disk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    return MEDIATYPE_UNKNOWN;
}

// Destructor
cx16Disk::~cx16Disk()
{
    if (_disk != nullptr)
        delete _disk;
}

// Unmount disk file
void cx16Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        _disk->unmount();
        device_active = false;
    }
}

// Create blank disk
bool cx16Disk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    return true;
}

// Process command
void cx16Disk::process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    if (_disk == nullptr || _disk->_mediatype == MEDIATYPE_UNKNOWN)
        return;

    if (device_active == false &&
        (cmdFrame.comnd != SIO_DISKCMD_STATUS && cmdFrame.comnd != SIO_DISKCMD_HSIO_INDEX))
        return;

    Debug_print("disk sio_process()\n");

    switch (cmdFrame.comnd)
    {
    case SIO_DISKCMD_READ:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_media_last_block)
        {
            cx16_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            cx16_nak();
            return;
        }
        else
        {
            cx16_ack();
            sio_read();
        }
        return;
    case SIO_DISKCMD_HSIO_READ:
        if (_disk->_allow_hsio)
        {
            cx16_ack();
            sio_read();
            return;
        }
        break;
    case SIO_DISKCMD_PUT:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_media_last_block)
        {
            cx16_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            cx16_nak();
            return;
        }
        else
        {
            cx16_ack();
            sio_write(false);
        }
        return;
    case SIO_DISKCMD_HSIO_PUT:
        if (_disk->_allow_hsio)
        {
            if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_media_last_block)
            {
                cx16_nak();
                return;
            }
            else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
            {
                cx16_nak();
                return;
            }
            else
            {
                cx16_ack();
                sio_write(false);
            }
        }
        break;
    case SIO_DISKCMD_STATUS:
    case SIO_DISKCMD_HSIO_STATUS:
        if (is_config_device == true)
        {
            if (theFuji.boot_config == true)
            {
            }
        }
        else
        {
            if (cmdFrame.comnd == SIO_DISKCMD_HSIO_STATUS && _disk->_allow_hsio == false)
                break;
            cx16_ack();
            status();
        }
        return;
    case SIO_DISKCMD_WRITE:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_media_last_block)
        {
            cx16_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            cx16_nak();
            return;
        }
        else
        {
            cx16_ack();
            sio_write(true);
        }
        return;
    case SIO_DISKCMD_HSIO_WRITE:
        if (_disk->_allow_hsio)
        {
            if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_media_last_block)
            {
                cx16_nak();
                return;
            }
            else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
            {
                cx16_nak();
                return;
            }
            else
            {
                cx16_ack();
                sio_write(true);
            }
            return;
        }
        break;
    case SIO_DISKCMD_FORMAT:
    case SIO_DISKCMD_FORMAT_MEDIUM:
        cx16_ack();
        sio_format();
        return;
    case SIO_DISKCMD_HSIO_FORMAT:
    case SIO_DISKCMD_HSIO_FORMAT_MEDIUM:
        if (_disk->_allow_hsio)
        {
            cx16_ack();
            sio_format();
            return;
        }
        break;
    }

    cx16_nak();
}

#endif /* BUILD_CX16 */