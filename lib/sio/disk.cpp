#include <memory.h>
#include <string.h>

//#include "fnSystem.h"
#include "../../include/debug.h"
#include "../utils/utils.h"

#include "disk.h"
#include "diskTypeAtr.h"
#include "diskTypeXex.h"

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

// Read disk data and send to computer
void sioDisk::sio_read()
{
    Debug_print("disk READ\n");

    if (_disk == nullptr)
    {
        sio_error();
        return;
    }

    uint16_t readcount;

    bool err = _disk->read(UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1), &readcount);

    // Send result to Atari
    sio_to_computer(_disk->_sectorbuff, readcount, err);
}

// Write disk data from computer
void sioDisk::sio_write(bool verify)
{
    Debug_print("disk WRITE\n");

    if (_disk != nullptr)
    {
        uint16_t sectorNum = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);
        uint16_t sectorSize = _disk->sector_size(sectorNum);

        memset(_disk->_sectorbuff, 0, DISK_SECTORBUF_SIZE);

        uint8_t ck = sio_to_peripheral(_disk->_sectorbuff, sectorSize);

        if (ck == sio_checksum(_disk->_sectorbuff, sectorSize))
        {
            if (_disk->write(sectorNum, verify) == false)
            {
                sio_complete();
                return;
            }
        }
    }

    sio_error();
}

// Status
void sioDisk::sio_status()
{
    Debug_print("disk STATUS\n");

    /* STATUS BYTES
        #0 - Drive status
            Bit 7 = 1: 26 sectors per track (1050/XF551 drive)
            Bit 6 = 1: Double sided disk (XF551 drive)
            Bit 5 = 1: Double density (XF551 drive)
            Bit 4 = 1: Motor running (always 0 on XF551)

            Bit 3 = 1: Failed due to write protected disk
            Bit 2 = 1: Unsuccessful PUT operation
            Bit 1 = 1: Receive error on last data frame (XF551)
            Bit 0 = 1: REceive error on last command frame (XF551)

        #1 - Floppy drive controller status (inverted from FDC)
            Bit 7 = 0: Not ready (1050 drive)
            Bit 6 = 0: Write protect error
            Bit 5 = 0: Deleted sector (sector marked as deleted in sector header)
            Bit 4 = 0: Record not found (missing sector)

            Bit 3 = 0: CRC error
            Bit 2 = 0: Lost data
            Bit 1 = 0: Data pending
            Bit 0 = 0: Busy

        #2 - Default timeout
              810 drive: $E0 = 224 vertical blanks
            XF551 drive: $FE

        #3 - Unused ($00)    
    */
    // TODO: Why $DF for second byte? 
    // TODO: Set bit 4 of drive status and bit 6 of FDC status on read-only disk
    uint8_t _status[4] = {0x00, 0xFF, 0xFE, 0x00};

    if (_disk != nullptr)
        _disk->status(_status);

    sio_to_computer(_status, sizeof(_status), false);
}

// Disk format
void sioDisk::sio_format()
{
    Debug_print("disk FORMAT\n");

    if (_disk == nullptr)
    {
        sio_error();
        return;
    }

    uint16_t responsesize;
    bool err = _disk->format(&responsesize);

    // Send to computer
    sio_to_computer(_disk->_sectorbuff, responsesize, err);
}

// Read percom block
void sioDisk::sio_read_percom_block()
{
    Debug_print("disk READ PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        sio_error();
        return;
    }

#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    sio_to_computer((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock), false);
}

// Write percom block
void sioDisk::sio_write_percom_block()
{
    Debug_print("disk WRITE PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        sio_error();
        return;
    }

    sio_to_peripheral((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock));
#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    sio_complete();
}

/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not DISKTYPE_UNKNOWN then that's used instead.
   If filename has no extension or is NULL and disk_type is DISKTYPE_UNKOWN,
   then we assume it's DISKTYPE_ATR.
   Return value is DISKTYPE_UNKNOWN in case of failure.
*/
disktype_t sioDisk::mount(FILE *f, const char *filename, uint32_t disksize, disktype_t disk_type)
{
    Debug_print("disk MOUNT\n");

    // Destroy any existing DiskType
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }

    // Determine DiskType based on filename extension
    if (disk_type == DISKTYPE_UNKNOWN && filename != nullptr)
    {
        
        int l = strlen(filename);
        if(l > 4 && filename[l - 4] == '.')
        {
            // Check the last 3 characters of the string
            l = l - 3;
            if(strcasecmp(filename + l, "COM") == 0) {
                disk_type = DISKTYPE_XEX;
            } else if(strcasecmp(filename + l, "XEX") == 0) {
                disk_type = DISKTYPE_XEX;
            } else if(strcasecmp(filename + l, "BIN") == 0) {
                disk_type = DISKTYPE_XEX;
            }
        }
    }

    // Now mount based on DiskType
    switch (disk_type)
    {
    case DISKTYPE_XEX:
        _disk = new DiskTypeXEX();
        return _disk->mount(f, disksize);
    case DISKTYPE_ATR:
    case DISKTYPE_UNKNOWN:
    default:
        _disk = new DiskTypeATR();
        return _disk->mount(f, disksize);
    }
}

// Destructor
sioDisk::~sioDisk()
{
    if (_disk != nullptr)
        delete _disk;
}

// Unmount disk file
void sioDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
        _disk->unmount();
}

// Create blank disk
bool sioDisk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return DiskTypeATR::create(f, sectorSize, numSectors);
}

// Process command
void sioDisk::sio_process()
{
    if (_disk == nullptr || _disk->_disktype == DISKTYPE_UNKNOWN)
        return;

    if (device_active == false &&
        (cmdFrame.comnd != SIO_DISKCMD_STATUS && cmdFrame.comnd != SIO_DISKCMD_HSIO_INDEX))
        return;

    Debug_print("disk sio_process()\n");

    switch (cmdFrame.comnd)
    {
    case SIO_DISKCMD_READ:
    case SIO_DISKCMD_HSIO_READ:
        sio_ack();
        sio_read();
        break;
    case SIO_DISKCMD_PUT:
    case SIO_DISKCMD_HSIO_PUT:
        sio_ack();
        sio_write(false);
        break;
    case SIO_DISKCMD_STATUS:
    case SIO_DISKCMD_HSIO_STATUS:
        if (is_config_device == true)
        {
            if (status_wait_count == 0)
            {
                device_active = true;
                sio_ack();
                sio_status();
            }
            else
            {
                Debug_print("ignoring status command\n");
                status_wait_count--;
            }
        }
        else
        {
            sio_ack();
            sio_status();
        }
        break;
    case SIO_DISKCMD_WRITE:
    case SIO_DISKCMD_HSIO_WRITE:
        sio_ack();
        sio_write(true);
        break;
    case SIO_DISKCMD_FORMAT:
    case SIO_DISKCMD_FORMAT_MEDIUM:
    case SIO_DISKCMD_HSIO_FORMAT:
    case SIO_DISKCMD_HSIO_FORMAT_MEDIUM:
        sio_ack();
        sio_format();
        break;
    case SIO_DISKCMD_PERCOM_READ:
        sio_ack();
        sio_read_percom_block();
        break;
    case SIO_DISKCMD_PERCOM_WRITE:
        sio_ack();
        sio_write_percom_block();
        break;
    case SIO_DISKCMD_HSIO_INDEX:
        sio_ack();
        sio_high_speed();
        break;
    default:
        sio_nak();
    }
}
