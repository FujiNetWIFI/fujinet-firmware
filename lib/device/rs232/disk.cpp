#ifdef BUILD_RS232

#include "disk.h"
#include "fujiCommandID.h"
#include "rom/ROMFactory.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "compat_string.h"
#include "fujiDevice.h"
#include "utils.h"

#define ROM_IMAGE_SEGMENT 0

// Legacy OPEN data, read only by the game console carts that don't use
// fujiversal; they look here instead of at OPEN's parameters. isMapperInfo is
// a flag, set when the data that follows is the .cfg mapper rather than the
// image. size lets the cart refuse an image bigger than its buffer, without
// FujiNet needing to know how big that is.
struct ConsoleCartOpenData
{
    bool isMapperInfo;
    u32le_t size;
};
static_assert(sizeof(ConsoleCartOpenData) == 5, "ConsoleCartOpenData must not be padded");

static ByteBuffer console_cart_open_data(bool isMapperInfo, uint32_t size)
{
    ConsoleCartOpenData openData;
    openData.isMapperInfo = isMapperInfo;
    openData.size = size;
    const uint8_t *bytes = (const uint8_t *)&openData;
    return ByteBuffer(bytes, bytes + sizeof(openData));
}

// Opens segment 0 and sends the image. CLOSE always goes out, so a later WRITE
// can't land in a segment left open. Game consoles don't watch a status
// register for the transfer to finish, so the DBC activates the ROM itself on
// CLOSE; after a failure CLOSE carries a 0x01 data byte telling the DBC to abort
// activation instead.
static error_is_true push_image(ROMMedia &rom)
{
    uint32_t expected_size = rom.size();
    if (expected_size == 0)
    {
        Debug_printv("ROM push: image is empty or unreadable\n");
        RETURN_ERROR_AS_TRUE();
    }

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN,
                                        (uint16_t)ROM_IMAGE_SEGMENT,
                                        console_cart_open_data(false, expected_size));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: failed to open image segment (%lu bytes)\n",
                     (unsigned long)expected_size);
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true result(false);
    ByteBuffer buffer;
    uint32_t sent = 0;
    while (sent < expected_size)
    {
        auto got = rom.read(buffer, DISK_SECTORBUF_SIZE);
        if (got.is_error() || buffer.empty())
            break; // the size check below reports it

        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE, buffer);
        if (!reply || reply->command() != CMD::FUJI_ACK)
        {
            Debug_printv("ROM push: failed to send image block\n");
            result = error_is_true(true);
            break;
        }
        sent += buffer.size();
    }

    if (result.is_success() && sent != expected_size)
    {
        Debug_printv("ROM push: image short transfer: %lu of %lu bytes\n",
                     (unsigned long)sent, (unsigned long)expected_size);
        result = error_is_true(true);
    }

    if (result.is_success())
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE, ByteBuffer{0x01});
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: image close failed/rejected\n");
        result = error_is_true(true);
    }
    return result;
}

// Sends mapper info between its own OPEN and CLOSE, with isMapperInfo set and
// no segment, since it isn't ROM data. It has to reach the DBC before the
// image, whose CLOSE activates the ROM on game consoles.
static error_is_true push_mapper(const std::pmr::vector<uint8_t> &mapper)
{
    if (mapper.empty())
    {
        Debug_printv("ROM push: mapper info is empty\n");
        RETURN_ERROR_AS_TRUE();
    }

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN,
                                        console_cart_open_data(true, mapper.size()));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: failed to open mapper info (%lu bytes)\n",
                     (unsigned long)mapper.size());
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true result(false);
    for (size_t offset = 0; offset < mapper.size(); offset += DISK_SECTORBUF_SIZE)
    {
        size_t count = std::min((size_t)DISK_SECTORBUF_SIZE, mapper.size() - offset);
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                       ByteBuffer(mapper.begin() + offset,
                                                  mapper.begin() + offset + count));
        if (!reply || reply->command() != CMD::FUJI_ACK)
        {
            Debug_printv("ROM push: failed to send mapper info block\n");
            result = error_is_true(true);
            break;
        }
    }

    if (result.is_success())
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE, ByteBuffer{0x01});
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: mapper info close failed/rejected\n");
        result = error_is_true(true);
    }
    return result;
}

// The whole ROM goes over at mount time, with any mapper info first.
static error_is_true push_rom_streams(ROMMedia &rom)
{
    auto mapper = rom.mapperInfo();
    if (mapper.has_value() && push_mapper(*mapper).is_error())
    {
        Debug_printv("ROM push: mapper info push failed\n");
        RETURN_ERROR_AS_TRUE();
    }

    if (push_image(rom).is_error())
    {
        Debug_printv("ROM push: ROM push failed\n");
        RETURN_ERROR_AS_TRUE();
    }

    RETURN_SUCCESS_AS_FALSE();
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

    // unmount() closes the image but keeps _disk, so check device_active too
    if (_disk == nullptr || !device_active)
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

    if (_disk != nullptr && device_active)
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
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
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
   A disk_type other than MEDIATYPE_UNKNOWN is used as given. Otherwise the type
   comes from the filename, or from the contents when the name says nothing, and
   an image of unknown type mounts raw.
   Return value is MEDIATYPE_UNKNOWN in case of failure.
*/
mediatype_t rs232Disk::mount(fnFile *f, const char *filename, uint32_t disksize,
                             disk_access_flags_t access_mode, mediatype_t disk_type,
                             fujiHost *host)
{
    Debug_print("disk MOUNT\n");

    // Destroy any existing MediaType
    if (_disk != nullptr)
    {
        delete _disk;
        _disk = nullptr;
    }

    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(f, filename, disksize);

    // Now mount based on MediaType
    switch (disk_type)
    {
    case MEDIATYPE_ROM:
    {
        // A ROM isn't a disk: once it's on the DBC the drive has no media
        // and the file isn't read again.
        device_active = false;
        _mount_time = 0;
        // Strongest evidence first: a signature, a mapper file, then the
        // extension alone.
        auto rom = ROMFactory::open<
            IntellicartROM,
            NESROM,
            IntellivisionROM,
            Atari7800ROM,
            StudioIIROM,
            Atari5200ROM,
            Atari2600ROM,
            SegaMasterROM,
            ChannelFROM,
            ColecoROM
            >(filename != nullptr ? filename : "", f, host);
        if (!rom)
        {
            fnio::fclose(f);
            return MEDIATYPE_UNKNOWN;
        }

        error_is_true pushed = push_rom_streams(*rom);
        rom->close();
        return pushed.is_success() ? MEDIATYPE_ROM : MEDIATYPE_UNKNOWN;
    }
    case MEDIATYPE_IMD:
    {
        device_active = true;
        _mount_time = time(nullptr);
        MediaTypeIMD *imd = new MediaTypeIMD((access_mode & DISK_ACCESS_MODE_WRITE) != 0);
        _disk = imd;
        imd->_media_host = host;
        if (filename != nullptr)
            strlcpy(imd->_disk_filename, filename, sizeof(imd->_disk_filename));
        return imd->mount(f, disksize);
    }
    case MEDIATYPE_ALTAIR:
    {
        device_active = true;
        _mount_time = time(nullptr);
        MediaTypeAltair *altair = new MediaTypeAltair((access_mode & DISK_ACCESS_MODE_WRITE) != 0);
        _disk = altair;
        altair->_media_host = host;
        if (filename != nullptr)
            strlcpy(altair->_disk_filename, filename, sizeof(altair->_disk_filename));
        return altair->mount(f, disksize);
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
success_is_true rs232Disk::write_blank(fnFile *f, const NewDiskRequest &req)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    if (MediaType::discover_mediatype(req.filename, (uint32_t)req.sectorSize * req.numSectors)
        == MEDIATYPE_IMD)
        return MediaTypeIMD::create(f, req.sectorSize, req.numSectors);
    return MediaTypeImg::create(f, req.sectorSize, req.numSectors);
}

// Process command
void rs232Disk::rs232_process(const FujiBusPacket &packet)
{
    Debug_print("disk rs232_process()\n");

    switch (packet.command())
    {
    case CMD::DISK_READ:
    case CMD::DISK_PUT:
    case CMD::DISK_WRITE:
        // The sector number is the first field; without one, param(0) would
        // throw (and with exceptions off, abort)
        if (packet.paramCount() < 1)
            break;
        if (packet.command() == CMD::DISK_READ)
            rs232_read(packet.param(0));
        else
            rs232_write(packet.param(0), packet.command() == CMD::DISK_WRITE);
        return;
    case CMD::DISK_STATUS:
        rs232_status(packet.paramCount() ? static_cast<FujiStatusReq>(packet.param(0))
                                         : FujiStatusReq::CONNERR);
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
