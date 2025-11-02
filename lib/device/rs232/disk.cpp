#ifdef BUILD_RS232

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "rs232Fuji.h"
#include "utils.h"

rs232Disk::rs232Disk()
{
    device_active = false;
    mount_time = 0;
}

// Read disk data and send to computer
void rs232Disk::rs232_read(uint32_t sector)
{
    Debug_printf("disk READ %lu\n", sector);

    if (_disk == nullptr)
    {
        rs232_error();
        return;
    }

    uint32_t readcount;

    bool err = _disk->read(sector, &readcount);

    // Send result to Atari
    bus_to_computer(_disk->_disk_sectorbuff, readcount, err);
}

// Write disk data from computer
void rs232Disk::rs232_write(uint32_t sector, bool verify)
{
    //Debug_print("disk WRITE\n");

    if (_disk != nullptr)
    {
        uint16_t sectorSize = _disk->sector_size(sector);

        memset(_disk->_disk_sectorbuff, 0, DISK_SECTORBUF_SIZE);

        uint8_t ck = bus_to_peripheral(_disk->_disk_sectorbuff, sectorSize);

        if (ck == rs232_checksum(_disk->_disk_sectorbuff, sectorSize))
        {
            if (_disk->write(sector, verify) == false)
            {
                rs232_complete();
                return;
            }
        }
    }

    rs232_error();
}

// Status
void rs232Disk::rs232_status(FujiStatusReq reqType)
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
    _status[1] = ~DISK_CTRL_STATUS_CLEAR; // Negation of default clear status
    _status[2] = DRIVE_DEFAULT_TIMEOUT_810;
    _status[3] = 0x00;

    if (_disk != nullptr)
        _disk->status(_status);

    Debug_printf("response: 0x%02x, 0x%02x, 0x%02x\n", _status[0], _status[1], _status[2]);

    bus_to_computer(_status, sizeof(_status), false);
}

// Disk format
void rs232Disk::rs232_format()
{
    Debug_print("disk FORMAT\n");

    if (_disk == nullptr)
    {
        rs232_error();
        return;
    }

    uint32_t responsesize;
    bool err = _disk->format(&responsesize);

    // Send to computer
    bus_to_computer(_disk->_disk_sectorbuff, responsesize, err);
}

// Read percom block
void rs232Disk::rs232_read_percom_block()
{
    Debug_print("disk READ PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        rs232_error();
        return;
    }

#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    bus_to_computer((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock), false);
}

// Write percom block
void rs232Disk::rs232_write_percom_block()
{
    Debug_print("disk WRITE PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        rs232_error();
        return;
    }

    bus_to_peripheral((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock));
#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    rs232_complete();
}

/* Mount Disk
   We determine the type of image based on the filename extenrs232n.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extenrs232n or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t rs232Disk::mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
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

    // Determine MediaType based on filename extenrs232n
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    // Now mount based on MediaType
    switch (disk_type)
    {
    case MEDIATYPE_IMG:
    case MEDIATYPE_UNKNOWN:
    default:
        device_active = true;
        mount_time = time(NULL);
        _disk = new MediaTypeImg();
        return _disk->mount(f, disksize);
    }
}

// Destructor
rs232Disk::~rs232Disk()
{
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }
}

// Unmount disk file
void rs232Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_disk != nullptr)
    {
        _disk->unmount();
        device_active = false;
        mount_time = 0;
    }
}

// Create blank disk
bool rs232Disk::write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return MediaTypeImg::create(f, sectorSize, numSectors);
}

// Process command
void rs232Disk::rs232_process(FujiBusPacket &packet)
{
    Debug_print("disk rs232_process()\n");

    switch (packet.command())
    {
    case FUJICMD_READ:
        rs232_ack();
        rs232_read(packet.param(0));
        return;
    case FUJICMD_PUT:
        rs232_ack();
        rs232_write(packet.param(0), false);
        return;
    case FUJICMD_STATUS:
    case FUJICMD_WRITE:
        rs232_ack();
        rs232_write(packet.param(0), true);
        return;
    case FUJICMD_FORMAT:
    case FUJICMD_FORMAT_MEDIUM:
        rs232_ack();
        rs232_format();
        return;
    case FUJICMD_PERCOM_READ:
        rs232_ack();
        rs232_read_percom_block();
        return;
    case FUJICMD_PERCOM_WRITE:
        rs232_ack();
        rs232_write_percom_block();
        return;
    default:
        break;
    }

    rs232_nak();
}

#endif /* BUILD_RS232 */
