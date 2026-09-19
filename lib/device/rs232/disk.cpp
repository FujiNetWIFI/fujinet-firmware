#ifdef BUILD_RS232

#include "disk.h"
#include "fujiCommandID.h"

#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "compat_string.h"
#include "fujiDevice.h"
#include "utils.h"

#define ROM_PUSH_STREAM_MAP   1
#define ROM_PUSH_STREAM_IMAGE 0

// Relays `source` as NET_WRITE frames on DBC stream `stream_id`, which the
// RP2040's dbc_inbound_handler() demuxes on.
//
// The OPEN header (stream id, then size as 4 LE bytes) travels as payload, not
// params: the RP2040's fujibus.c skips the descriptor chain and exposes only
// payload to callers. It uses the size to reject an oversized ROM before we
// pull it over TNFS; older builds read data[0] and ignore the rest.
//
// CLOSE always goes out or the RP2040's stream state wedges; on failure it
// carries a 0x01 abort so partial data isn't booted.
static bool push_stream(MediaTypeROM *rom, RomStream source, uint16_t stream_id)
{
    uint8_t buffer[DISK_SECTORBUF_SIZE];
    uint32_t expected_size = rom->stream_size(source);

    if (expected_size == 0)
    {
        Debug_printv("ROM push: stream %u is empty or unreadable\n", stream_id);
        return false;
    }

    struct { uint8_t id; u32le_t size; } open_hdr;
    static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
    open_hdr.id = (uint8_t)stream_id;
    open_hdr.size = expected_size;

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN,
                                        std::string((const char *)&open_hdr, sizeof(open_hdr)));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: failed to open DBC stream %u (%lu bytes)\n",
                     stream_id, (unsigned long)expected_size);
        return false;
    }

    bool ok = true;
    uint32_t sent = 0;
    while (sent < expected_size)
    {
        size_t got = rom->stream_read(source, sent, buffer, sizeof(buffer));
        if (got == 0)
            break; // the size check below reports it

        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                       std::string((char *)buffer, got));
        if (!reply || reply->command() != CMD::FUJI_ACK)
        {
            Debug_printv("ROM push: failed to send stream %u block\n", stream_id);
            ok = false;
            break;
        }
        sent += got;
    }

    if (ok && sent != expected_size)
    {
        Debug_printv("ROM push: stream %u short transfer: %lu of %lu bytes\n",
                     stream_id, (unsigned long)sent, (unsigned long)expected_size);
        ok = false;
    }

    if (ok)
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE,
                                       std::string(1, '\x01'));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: stream %u close failed/rejected\n", stream_id);
        ok = false;
    }
    return ok;
}

// The whole ROM goes over at mount time. The image's CLOSE triggers the boot,
// so the map has to land first.
static bool push_rom_streams(MediaTypeROM *rom)
{
    if (rom->has_memory_map() &&
        !push_stream(rom, RomStream::MemoryMap, ROM_PUSH_STREAM_MAP))
    {
        Debug_printv("ROM push: memory map push failed\n");
        return false;
    }

    if (!push_stream(rom, RomStream::Image, ROM_PUSH_STREAM_IMAGE))
    {
        Debug_printv("ROM push: ROM push failed\n");
        return false;
    }

    return true;
}

rs232Disk::rs232Disk()
{
    device_active = false;
    _mount_time = 0;
}

// Read disk data and send to computer
void rs232Disk::rs232_read(uint32_t sector)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("disk READ %lu\n", sector);

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    uint32_t readcount;

    bool err = _disk->read(sector, &readcount);

    // Send result to Atari
    SYSTEM_BUS.transaction_send(_disk->_disk_sectorbuff, readcount, err);
}

// Write disk data from computer
void rs232Disk::rs232_write(uint32_t sector, bool verify)
{
    //Debug_print("disk WRITE\n");

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (_disk != nullptr)
    {
        uint16_t sectorSize = _disk->sector_size(sector);

        memset(_disk->_disk_sectorbuff, 0, DISK_SECTORBUF_SIZE);

        if (SYSTEM_BUS.transaction_get(_disk->_disk_sectorbuff, sectorSize))
        {
            if (_disk->write(sector, verify) == false)
            {
                SYSTEM_BUS.transaction_success();
                return;
            }
        }
    }

    SYSTEM_BUS.transaction_error();
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

    SYSTEM_BUS.transaction_send(_status, sizeof(_status), false);
}

// Disk format
void rs232Disk::rs232_format()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk FORMAT\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    uint32_t responsesize;
    bool err = _disk->format(&responsesize);

    // Send to computer
    SYSTEM_BUS.transaction_send(_disk->_disk_sectorbuff, responsesize, err);
}

// Read percom block
void rs232Disk::rs232_read_percom_block()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk READ PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    SYSTEM_BUS.transaction_send((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock), false);
}

// Write percom block
void rs232Disk::rs232_write_percom_block()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    Debug_print("disk WRITE PERCOM BLOCK\n");

    if (_disk == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_get((uint8_t *)&_disk->_percomBlock, sizeof(_disk->_percomBlock));
#ifdef VERBOSE_DISK
    _disk->dump_percom_block();
#endif
    SYSTEM_BUS.transaction_success();
}

/* Mount Disk
   We determine the type of image based on the filename extenrs232n.
   If the disk_type value passed is not MEDIATYPE_UNKNOWN then that's used instead.
   If filename has no extenrs232n or is NULL and disk_type is MEDIATYPE_UNKOWN,
   then we assume it's MEDIATYPE_ATR.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t rs232Disk::mount(fnFile *f, const char *filename, uint32_t disksize,
                             disk_access_flags_t access_mode, mediatype_t disk_type,
                             fujiHost *host)
{
    // TAPE or CASSETTE: use this function to send file info to cassette device
    //  MediaType::discover_mediatype(filename) can detect CAS and WAV files
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
    case MEDIATYPE_ROM:
    {
        device_active = true;
        _mount_time = time(NULL);
        MediaTypeROM *rom = new MediaTypeROM();
        _disk = rom;
        rom->_media_host = host;
        if (filename != nullptr)
            strlcpy(rom->_disk_filename, filename, sizeof(rom->_disk_filename));
        rom->mount(f, disksize);
        return push_rom_streams(rom) ? MEDIATYPE_ROM : MEDIATYPE_UNKNOWN;
    }
    case MEDIATYPE_IMG:
    case MEDIATYPE_UNKNOWN:
    default:
        device_active = true;
        _mount_time = time(NULL);
        _disk = new MediaTypeImg();
        return _disk->mount(f, disksize);
    }
}

mediatype_t rs232Disk::mount_disk_media(fnFile *f, const char *filename, uint32_t disksize,
                                         mediatype_t disk_type)
{
    return mount(f, filename, disksize, DISK_ACCESS_MODE_READ, disk_type);
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
        _mount_time = 0;
    }
}

// Create blank disk
success_is_true rs232Disk::write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    return MediaTypeImg::create(f, sectorSize, numSectors);
}

// Process command
void rs232Disk::rs232_process(const FujiBusPacket &packet)
{
    Debug_print("disk rs232_process()\n");

    switch (packet.command())
    {
    case CMD::DISK_READ:
        rs232_read(packet.param(0));
        return;
    case CMD::DISK_PUT:
        rs232_write(packet.param(0), false);
        return;
    case CMD::DISK_STATUS:
    case CMD::DISK_WRITE:
        rs232_write(packet.param(0), true);
        return;
    case CMD::DISK_FORMAT:
    case CMD::DISK_FORMAT_MEDIUM:
        rs232_format();
        return;
    case CMD::DISK_PERCOM_READ:
        rs232_read_percom_block();
        return;
    case CMD::DISK_PERCOM_WRITE:
        rs232_write_percom_block();
        return;
    default:
        break;
    }

    SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_RS232 */
