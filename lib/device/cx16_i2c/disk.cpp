#ifdef BUILD_CX16

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "fujiDevice.h"
#include "utils.h"

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
mediatype_t cx16Disk::mount(FILE *f, const char *filename, uint32_t disksize,
                            disk_access_flags_t access_mode, mediatype_t disk_type)
{
    return MEDIATYPE_UNKNOWN;
}

// Destructor
cx16Disk::~cx16Disk()
{
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }
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
        (cmdFrame.comnd != DISKCMD_STATUS && cmdFrame.comnd != DISKCMD_HSIO_INDEX))
        return;

    Debug_print("disk cx16Disk::process()\n");

    switch (cmdFrame.comnd)
    {
    case DISKCMD_READ:
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
    case DISKCMD_HSIO_READ:
        if (_disk->_allow_hsio)
        {
            cx16_ack();
            sio_read();
            return;
        }
        break;
    case DISKCMD_PUT:
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
    case DISKCMD_HSIO_PUT:
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
    case DISKCMD_STATUS:
    case DISKCMD_HSIO_STATUS:
        if (is_config_device == true)
        {
            if (theFuji->boot_config == true)
            {
            }
        }
        else
        {
            if (cmdFrame.comnd == DISKCMD_HSIO_STATUS && _disk->_allow_hsio == false)
                break;
            cx16_ack();
            status();
        }
        return;
    case DISKCMD_WRITE:
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
    case DISKCMD_HSIO_WRITE:
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
    case DISKCMD_FORMAT:
    case DISKCMD_FORMAT_MEDIUM:
        cx16_ack();
        sio_format();
        return;
    case DISKCMD_HSIO_FORMAT:
    case DISKCMD_HSIO_FORMAT_MEDIUM:
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
