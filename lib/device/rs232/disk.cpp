#ifdef BUILD_RS232

#include "disk.h"
#include "fujiCommandID.h"

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

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_OPEN,
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
        auto got = rom.read(buffer, rs232Disk::ROM_BLOCK_SIZE);
        if (got.is_error() || buffer.empty())
            break; // the size check below reports it

        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_WRITE, buffer);
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
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_CLOSE,
                                       ByteBuffer{0x01});
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

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_OPEN,
                                        console_cart_open_data(true, mapper.size()));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("ROM push: failed to open mapper info (%lu bytes)\n",
                     (unsigned long)mapper.size());
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true result(false);
    for (size_t offset = 0; offset < mapper.size(); offset += rs232Disk::ROM_BLOCK_SIZE)
    {
        size_t count = std::min((size_t)rs232Disk::ROM_BLOCK_SIZE, mapper.size() - offset);
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_WRITE,
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
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_CLOSE);
    else
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_CLOSE,
                                       ByteBuffer{0x01});
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

rs232Disk::~rs232Disk()
{
    unmount();
}

// Read disk data and send to computer
void rs232Disk::rs232_read(uint32_t sector)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    Debug_printf("disk READ %lu\n", sector);

    if (!isMounted())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    ByteBuffer buffer;
    bool err = _media->readBlock(sector, buffer).is_error();
    SYSTEM_BUS.transaction_send(buffer.data(), err ? 0 : buffer.size(), err);
}

// Write disk data from computer
void rs232Disk::rs232_write(uint32_t sector, bool /*verify*/)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    uint16_t size = 0;
    ByteBuffer buffer;
    if (isMounted())
        size = _media->blockSize(sector);
    if (size > 0)
    {
        buffer.assign(size, 0);
        if (SYSTEM_BUS.transaction_get(buffer.data(), size).is_success()
            && _media->writeBlock(sector, buffer).is_success())
        {
            SYSTEM_BUS.transaction_success();
            return;
        }
    }

    SYSTEM_BUS.transaction_error();
}

// Four bytes as an Atari 810 or XF551 reports them; only the bits the geometry
// and write protection decide are set.
void rs232Disk::rs232_status(FujiStatusReq /*reqType*/)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk STATUS\n");

    constexpr uint8_t FORMAT_TIMEOUT_810 = 0xE0;
    uint8_t driveStatus = 0;
    uint8_t controllerStatus = 0;

    if (isMounted())
    {
        PercomBlock geometry = derivePercom();
        if (geometry.density == PERCOM_DENSITY_MFM)
            driveStatus |= DRIVE_STATUS_DOUBLE_DENSITY;
        if (geometry.numSides == 1)
            driveStatus |= DRIVE_STATUS_DOUBLE_SIDED;
        if (geometry.sectorsPerTrack == 26)
            driveStatus |= DRIVE_STATUS_ENHANCED_DENSITY;
        if (_media->isReadOnly())
        {
            driveStatus |= DRIVE_STATUS_WRITE_PROTECT;
            controllerStatus |= CONTROLLER_STATUS_WRITE_PROTECT;
        }
    }

    // The controller byte is sent inverted, as the FDC reports it.
    uint8_t status[4] = {driveStatus, (uint8_t)~controllerStatus, FORMAT_TIMEOUT_810, 0};
    SYSTEM_BUS.transaction_send(status, sizeof(status), false);
}

// Nothing on this bus formats a disk image.
void rs232Disk::rs232_format()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk FORMAT not supported\n");
    SYSTEM_BUS.transaction_error();
}

// Taken from the data tracks: track 0 is often a different format from the
// rest, so it counts only on a single-track disk.
rs232Disk::PercomBlock rs232Disk::derivePercom()
{
    PercomBlock geometry = {};
    if (!isMounted() || _media->cylinderCount() == 0)
        return geometry;

    uint16_t cylinders = _media->cylinderCount();
    uint8_t sides = _media->sideCount();
    uint8_t sectorsPerTrack = 0;
    uint16_t sectorSize = 0;
    bool mfm = false;
    for (uint16_t cylinder = cylinders > 1 ? 1 : 0; cylinder < cylinders; cylinder++)
    {
        for (uint8_t side = 0; side < sides; side++)
        {
            TrackGeometry track = _media->trackGeometry(cylinder, side);
            sectorsPerTrack = std::max(sectorsPerTrack, track.sectorCount);
            sectorSize = std::max(sectorSize, track.sectorSize);
            mfm = mfm || track.encoding == TrackEncoding::MFM;
        }
    }

    geometry.numTracks = cylinders > UINT8_MAX ? 0 : (uint8_t)cylinders;
    geometry.stepRate = 0x01;
    geometry.sectorsPerTrack = sectorsPerTrack;
    geometry.numSides = sides - 1;
    geometry.density = mfm ? PERCOM_DENSITY_MFM : PERCOM_DENSITY_FM;
    geometry.sectorSize = sectorSize;
    geometry.drivePresent = 0xFF;

    return geometry;
}

void rs232Disk::rs232_read_percom_block()
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_print("disk READ PERCOM BLOCK\n");

    if (!isMounted())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    PercomBlock geometry = derivePercom();
    SYSTEM_BUS.transaction_send((uint8_t *)&geometry, sizeof(geometry), false);
}

// A cartridge goes to the DBC and leaves nothing mounted; anything no cartridge
// takes is a disk. Whatever accepts the file owns the handle; if nothing does,
// it's closed here.
error_is_true rs232Disk::mount(fnFile *f, const char *filename, uint32_t /*disksize*/,
                               disk_access_flags_t access_mode, fujiHost *host)
{
    Debug_print("disk MOUNT\n");

    unmount();

    std::string path = filename != nullptr ? filename : "";
    auto rom = Formats::open<ROMMedia>(path, f, host);
    if (rom)
    {
        error_is_true pushed = push_rom_streams(*rom);
        rom->close();
        return pushed;
    }

    bool readOnly = !(access_mode & DISK_ACCESS_MODE_WRITE);
    _media = Formats::open<BlockMedia>(path, f, host, readOnly);
    if (!_media)
    {
        fnio::fclose(f);
        RETURN_ERROR_AS_TRUE();
    }

    device_active = true;
    _mount_time = time(nullptr);
    RETURN_SUCCESS_AS_FALSE();
}

void rs232Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr)
    {
        _media->close();
        _media.reset();
    }
    device_active = false;
    _mount_time = 0;
}

// An IMD name gets a blank IBM-3740 image; anything else a raw one.
success_is_true rs232Disk::write_blank(fnFile *f, const NewDiskRequest &req)
{
    Debug_print("disk CREATE NEW IMAGE\n");

    if (Media::matchExtension(req.filename, IMDDisk::extensions).has_value())
        return IMDDisk::create(f, req.sectorSize, req.numSectors);
    return RawDisk::create(f, req.sectorSize, req.numSectors);
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
    default:
        break;
    }

    SYSTEM_BUS.transaction_error();
}

#endif /* BUILD_RS232 */
