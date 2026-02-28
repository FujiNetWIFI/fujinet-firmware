#ifdef BUILD_RC2014

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "media.h"
#include "utils.h"

rc2014Disk::rc2014Disk()
{
}

// Destructor
rc2014Disk::~rc2014Disk()
{
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }
}

// Read disk data and send to computer
void rc2014Disk::read()
{
    Debug_print("disk READ\n");

    if (_media == nullptr)
    {
        rc2014_send_error();
        return;
    }

    rc2014_send_ack();

    uint16_t readcount;

    Debug_printf("disk READ: sector = %d\n", UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1));

    bool err = _media->read(UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1), &readcount);

    Debug_printf("disk READ: readcount = %d\n", readcount);

    // Send result to RC2014
    rc2014_send_buffer(_media->_media_sectorbuff, readcount);
    rc2014_flush();

    rc2014_send_complete();
}

// Write disk data from computer
void rc2014Disk::write(bool verify)
{
    Debug_print("disk WRITE\n");
    rc2014_send_ack();

    if (_media != nullptr)
    {
        uint16_t sectorNum = UINT16_FROM_HILOBYTES(cmdFrame.aux2, cmdFrame.aux1);
        uint16_t sectorSize = _media->sector_size(sectorNum);

        memset(_media->_media_sectorbuff, 0, DISK_BYTES_PER_SECTOR_SINGLE);

        rc2014_recv_buffer(_media->_media_sectorbuff, sectorSize);
        // TODO: decide on whether we want to checksum the disk data
        // uint8_t ck = rc2014_recv(); // ck

        // if (ck == rc2014_checksum(_media->_media_sectorbuff, sectorSize))
        // {
            if (_media->write(sectorNum, verify) == false)
            {
                rc2014_send_complete();
                return;
            }
        // }
    }

    rc2014_send_error();
}

// Status
void rc2014Disk::status()
{
    Debug_print("disk STATUS\n");
    rc2014_send_ack();

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

    if (_media != nullptr)
        _media->status(_status);

    Debug_printf("response: 0x%02x, 0x%02x, 0x%02x\n", _status[0], _status[1], _status[2]);

    rc2014_send_buffer(_status, sizeof(_status));
    rc2014_flush();

    rc2014_send_complete();
}

// Disk format
void rc2014Disk::format()
{
    Debug_print("disk FORMAT\n");

    if (_media == nullptr)
    {
        rc2014_send_error();
        return;
    }

    rc2014_send_ack();

    uint16_t responsesize;
    bool err = _media->format(&responsesize);

    // Send to computer
    rc2014_send_buffer(_media->_media_sectorbuff, responsesize);
    rc2014_flush();

    rc2014_send_complete();
}

mediatype_t rc2014Disk::mount(FILE *f, const char *filename, uint32_t disksize,
                              disk_access_flags_t access_mode, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("disk MOUNT %s\n", filename);

    // Destroy any existing MediaType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename, disksize);

    if (disk_type != MEDIATYPE_UNKNOWN) {
        _media = new MediaTypeIMG();
        mt = _media->mount(f, disksize, disk_type);
        device_active = true;
        Debug_printf("disk MOUNT mediatype = %d: active\n", disk_type);
    } else {
        device_active = false;
        Debug_printf("disk MOUNT unknown: deactive\n");
    }

    return mt;
}

void rc2014Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr) {
        _media->unmount();
        device_active = false;
    }
}

void rc2014Disk::get_size()
{
    Debug_print("disk SIZE\n");
    rc2014_send_ack();

    uint32_t disk_size = 0;
    if (_media != nullptr)
        disk_size = _media->num_sectors();

    uint8_t sectors[4];
    sectors[0] = disk_size & 0xFF;
    sectors[1] = (disk_size >> 8) & 0xFF;
    sectors[2] = (disk_size >> 16) & 0xFF;
    sectors[3] = (disk_size >> 24) & 0xFF;

    Debug_printf("number of sectors: %lu\n", disk_size);

    rc2014_send_buffer(sectors, sizeof(sectors));
    rc2014_flush();

    rc2014_send_complete();
}

// Create blank disk
bool rc2014Disk::write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return true; //MediaTypeImg::create(f, sectorSize, numSectors);
}

void rc2014Disk::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    if (_media == nullptr || _media->_mediatype == MEDIATYPE_UNKNOWN)
        return;

    switch (cmdFrame.comnd) {
    case DISKCMD_READ:
        read();
        return;
    case DISKCMD_PUT:
        write(false);
        return;
    case DISKCMD_STATUS:
        status();
        return;
    case DISKCMD_WRITE:
        write(true);
        return;
    case DISKCMD_FORMAT:
    case DISKCMD_FORMAT_MEDIUM:
        format();
        return;
    case DISKCMD_SIZE:
        get_size();
        return;
    default:
        Debug_printf("rc2014_process() command not implemented. Cmd received: %02x\n", cmdFrame.comnd);
        rc2014_send_nak();
    }
}

#endif /* NEW_TARGET */
