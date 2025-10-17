#ifdef BUILD_ATARI

#include "disk.h"

#include <cstring>
#include <memory.h>

#include "../../include/debug.h"

#include "sioFuji.h"
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

sioDisk::sioDisk()
{
    device_active = false;
}

// Read disk data and send to computer
void sioDisk::sio_read()
{
    // Debug_print("disk READ\n");

    if (_disk == nullptr)
    {
        // Send error but dummy sector.
        uint8_t dummySector[128];
        memset(dummySector,0,sizeof(dummySector));
        bus_to_computer(dummySector,128,true);
        return;
    }

    uint16_t readcount;

    bool err = _disk->read(UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1), &readcount);

    // Send result to Atari
    bus_to_computer(_disk->_disk_sectorbuff, readcount, err);
}

// Write disk data from computer
void sioDisk::sio_write(bool verify)
{
    // Debug_print("disk WRITE\n");

    if (_disk != nullptr)
    {
        uint16_t sectorNum = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);
        uint16_t sectorSize = _disk->sector_size(sectorNum);

        memset(_disk->_disk_sectorbuff, 0, DISK_SECTORBUF_SIZE);

        uint8_t ck = bus_to_peripheral(_disk->_disk_sectorbuff, sectorSize);

        if (ck == sio_checksum(_disk->_disk_sectorbuff, sectorSize))
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
            Bit 0 = 1: Receive error on last command frame (XF551)

        #1 - Floppy drive controller status (inverted from FDC)
            Bit 7 = 0: Drive not ready (1050 drive)
            Bit 6 = 0: Write protect error
            Bit 5 = 0: Deleted sector (sector marked as deleted in sector header)
            Bit 4 = 0: Record not found (missing sector)

            Bit 3 = 0: CRC error
            Bit 2 = 0: Lost data
            Bit 1 = 0: Data request pending
            Bit 0 = 0: Busy

        #2 - Format timeout
              810 drive: $E0 = 224 vertical blanks (4 mins NTSC)
            XF551 drive: $FE = 254 veritcal blanks (4.5 mins NTSC)

        #3 - Unused ($00)
    */
    // TODO: Why $DF for second byte?
    // TODO: Set bit 4 of drive status and bit 6 of FDC status on read-only disk
#define DRIVE_DEFAULT_TIMEOUT_810 0xE0
#define DRIVE_DEFAULT_TIMEOUT_XF551 0xFE

    uint8_t _status[4];
    _status[0] = 0x00;
    
    if (_disk != nullptr)
    {
        if (_disk->_disk_num_sectors == 1040)
        {
            _status[0] |= 0x80; // 1050 density
        }

        if (_disk->_disk_sector_size == 256)
        {
            _status[0] |= 0x20; // Double density
        }

        if (_disk->_disk_num_sectors == 1440 || _disk->_disk_num_sectors == 2880)
        {
            _status[0] |= 0x40; // Double sided
        }
    }

    _status[1] = ~DISK_CTRL_STATUS_CLEAR; // Negation of default clear status
    _status[2] = DRIVE_DEFAULT_TIMEOUT_810;
    _status[3] = 0x00;

    if (_disk != nullptr)
        _disk->status(_status);

    Debug_printf("response: 0x%02x, 0x%02x, 0x%02x\n", _status[0], _status[1], _status[2]);

    bus_to_computer(_status, sizeof(_status), false);
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
    bus_to_computer(_disk->_disk_sectorbuff, responsesize, err);
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
    bus_to_computer((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock), false);
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

    bus_to_peripheral((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock));
#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    sio_complete();
}

/* Mount Disk
   We determine the type of image based on the filename extension.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extension or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t sioDisk::mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{
    // TAPE or CASSETTE: use this function to send file info to cassette device
    //  MediaType::discover_disktype(filename) can detect CAS and WAV files
    Debug_print("disk MOUNT\n");

    // Destroy any existing MediaType
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_disktype(filename);

    // Now mount based on MediaType
    switch (disk_type)
    {
    case MEDIATYPE_CAS:
    case MEDIATYPE_WAV:
        // open the cassette file
        platformFuji.cassette()->mount_cassette_file(f, disksize);
        return disk_type;
        // TODO left off here for tape cassette
        break;
    case MEDIATYPE_XEX:
        device_active = true;
        _disk = new MediaTypeXEX();
        if (host != nullptr)
        {
            _disk->_disk_host = host;
            strcpy(_disk->_disk_filename, filename);
        }
        return _disk->mount(f, disksize);
    case MEDIATYPE_ATX:
        device_active = true;
        _disk = new MediaTypeATX();
        if (host != nullptr)
        {
            _disk->_disk_host = host;
            strcpy(_disk->_disk_filename, filename);
        }
        return _disk->mount(f, disksize);
    case MEDIATYPE_ATR:
    case MEDIATYPE_UNKNOWN:
    default:
        device_active = true;
        _disk = new MediaTypeATR();
        if (host != nullptr)
        {
            _disk->_disk_host = host;
            strcpy(_disk->_disk_filename, filename);
        }
        return _disk->mount(f, disksize);
    }
}

// Destructor
sioDisk::~sioDisk()
{
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }
}

// Unmount disk file
void sioDisk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        _disk->unmount();
        device_active = false;
#ifndef ESP_PLATFORM // apc: good for ESP too?
        delete _disk;
        _disk = nullptr;
#endif
    }
}

// Create blank disk
bool sioDisk::write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return MediaTypeATR::create(f, sectorSize, numSectors);
}

// Process command
void sioDisk::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.cksum = checksum;

    if (_disk == nullptr || _disk->_disktype == MEDIATYPE_UNKNOWN)
        return;

    if ((device_active == false && cmdFrame.device != SIO_DEVICEID_DISK) || // not active and not D1
        (device_active == false && theFuji->boot_config == false)) // not active and not config boot
        return;

    Debug_printf("disk sio_process(), baud: %d\n", SYSTEM_BUS.getBaudrate());

    switch (cmdFrame.comnd)
    {
    case SIO_DISKCMD_READ:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_disk_num_sectors)
        {
            sio_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            sio_nak();
            return;
        }
        else
        {
            sio_ack();
            sio_read();
        }
        return;
    case SIO_DISKCMD_HSIO_READ:
        if (_disk->_allow_hsio)
        {
            sio_ack();
            sio_read();
            return;
        }
        break;
    case SIO_DISKCMD_PUT:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_disk_num_sectors)
        {
            sio_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            sio_nak();
            return;
        }
        else
        {
            sio_late_ack();
            sio_write(false);
        }
        return;
    case SIO_DISKCMD_HSIO_PUT:
        if (_disk->_allow_hsio)
        {
            if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_disk_num_sectors)
            {
                sio_nak();
                return;
            }
            else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
            {
                sio_nak();
                return;
            }
            else
            {
                sio_late_ack();
                sio_write(false);
            }
        }
        break;
    case SIO_DISKCMD_STATUS:
    case SIO_DISKCMD_HSIO_STATUS:
        if (is_config_device == true)
        {
            if (theFuji->boot_config == true)
            {
                if (status_wait_count > 0 && theFuji->status_wait_enabled)
                {
                    Debug_print("ignoring status command\n");
                    status_wait_count--;
                }
                else
                {
                    device_active = true;
                    sio_ack();
                    sio_status();
                }
            }
        }
        else
        {
            if (cmdFrame.comnd == SIO_DISKCMD_HSIO_STATUS && _disk->_allow_hsio == false)
                break;
            sio_ack();
            sio_status();
        }
        return;
    case SIO_DISKCMD_WRITE:
        if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_disk_num_sectors)
        {
            sio_nak();
            return;
        }
        else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
        {
            sio_nak();
            return;
        }
        else
        {
            sio_late_ack();
            sio_write(true);
        }
        return;
    case SIO_DISKCMD_HSIO_WRITE:
        if (_disk->_allow_hsio)
        {
            if (UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1) > _disk->_disk_num_sectors)
            {
                sio_nak();
                return;
            }
            else if ((cmdFrame.aux1 == 0) && (cmdFrame.aux2 == 0))
            {
                sio_nak();
                return;
            }
            else
            {
                sio_late_ack();
                sio_write(true);
            }
            return;
        }
        break;
    case SIO_DISKCMD_FORMAT:
    case SIO_DISKCMD_FORMAT_MEDIUM:
        sio_ack();
        sio_format();
        return;
    case SIO_DISKCMD_HSIO_FORMAT:
    case SIO_DISKCMD_HSIO_FORMAT_MEDIUM:
        if (_disk->_allow_hsio)
        {
            sio_ack();
            sio_format();
            return;
        }
        break;
    case SIO_DISKCMD_PERCOM_READ:
        sio_ack();
        sio_read_percom_block();
        return;
    case SIO_DISKCMD_PERCOM_WRITE:
        sio_late_ack();
        sio_write_percom_block();
        return;
    case SIO_DISKCMD_HSIO_INDEX:
        if (_disk->_allow_hsio)
        {
            sio_ack();
            sio_high_speed();
            SYSTEM_BUS.toggleBaudrate();
            return;
        }
        break;
    }

    sio_nak();
}

#endif /* BUILD_ATARI */
