#ifdef BUILD_COCO

#include "disk.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "bus.h"
#include "fujiCommandID.h"
#include "fujiDevice.h"
#include "utils.h"

#define SIZE_32K 32_kB

#define R_COCO 0b11000000

enum fujiROMType_t : uint8_t {
    ROM_TYPE_COCO_32K_FF90    = 0x01 | R_COCO,
    ROM_TYPE_COCO_64K_FF90    = 0x02 | R_COCO,
    ROM_TYPE_COCO_128K_FF90   = 0x03 | R_COCO,
};

// A ROM image isn't served block by block the way a disk is: the whole file
// goes to the cartridge's DBC at mount time, which presents it to the CoCo
// as cartridge ROM. Nothing reads it back through the media object afterwards.
static error_is_true push_rom_image(ROMMedia &rom)
{
#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
    if (SYSTEM_BUS.isBoIP())
    {
        Debug_printv("ROM media requires a real pico; not supported over BoIP");
        RETURN_ERROR_AS_TRUE();
    }

    uint32_t imagesize = rom.size();
    if (imagesize == 0)
    {
        Debug_printv("ROM image is empty or unreadable");
        RETURN_ERROR_AS_TRUE();
    }

    if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_OPEN, (uint16_t)0))
    {
        Debug_printv("Failed to open pico bank");
        RETURN_ERROR_AS_TRUE();
    }

    ByteBuffer blockbuff;
    size_t sent = 0;
    while (true)
    {
        auto got = rom.read(blockbuff, DRIVEWIRE_SECTOR_SIZE);
        if (got.is_error() || got == 0)
        {
            if (got.is_error())
                Debug_printv("ROM read short: sent %lu of %lu bytes",
                             (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_WRITE, blockbuff))
        {
            Debug_printv("Failed to send ROM block at %lu of %lu bytes", (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        sent += got;
    }

    // FIXME - Use CMD::DBC_MAPPER_ADD_WINDOW instead of sending ROM type
    ByteBuffer rtype(1);
    if (imagesize <= SIZE_32K)
        rtype[0] = ROM_TYPE_COCO_32K_FF90;
    else if (imagesize <= 2 * SIZE_32K)
        rtype[0] = ROM_TYPE_COCO_64K_FF90;
    else
        rtype[0] = ROM_TYPE_COCO_128K_FF90;
    SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_MAPPER_SET_INFO, rtype);

    SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::DBC_SEGMENT_CLOSE);
    Debug_printv("ROM transfer complete: %lu / %lu bytes", (unsigned long)sent, (unsigned long)imagesize);

    RETURN_ERROR_IF(sent != imagesize);
#else
    (void)rom;
    Debug_printv("ROM mount not supported on this FujiNet hardware.");
    RETURN_ERROR_AS_TRUE();
#endif
}

drivewireDisk::drivewireDisk()
{
    device_active = false;
}

drivewireDisk::~drivewireDisk()
{
    unmount();
}

// The disk formats are tried first, then a cartridge. Whatever accepts the
// file owns the handle; if nothing does, it's closed here.
error_is_true drivewireDisk::mount(fnFile *f, const char *filename, uint32_t /*disksize*/,
                                   disk_access_flags_t access_mode, fujiHost *host)
{
    Debug_printf("DW disk MOUNT %s\n", filename);

    unmount();

    std::string path = filename != nullptr ? filename : "";
    bool readOnly = !(access_mode & DISK_ACCESS_MODE_WRITE);
    _media = Formats::open<BlockMedia>(path, f, host, readOnly);
    if (_media)
    {
        _status = DISK_STATUS_OK;
        device_active = true;
        RETURN_SUCCESS_AS_FALSE();
    }

    auto rom = Formats::open<ROMMedia>(path, f, nullptr);
    if (!rom)
    {
        fnio::fclose(f);
        RETURN_ERROR_AS_TRUE();
    }

    error_is_true pushed = push_rom_image(*rom);
    rom->close();
    return pushed;
}

void drivewireDisk::unmount()
{
    Debug_print("DW disk UNMOUNT\n");

    if (_media)
    {
        _media->close();
        _media.reset();
    }
    device_active = false;
}

error_is_true drivewireDisk::read(uint32_t sector, ByteBuffer &data)
{
    if (!isMounted())
        RETURN_ERROR_AS_TRUE();

    // A block the media doesn't have is reported to the CoCo as EOF.
    if (_media->blockSize(sector) == 0)
    {
        _status = DISK_STATUS_EOF;
        RETURN_ERROR_AS_TRUE();
    }
    _status = DISK_STATUS_OK;

    return _media->readBlock(sector, data);
}

error_is_true drivewireDisk::write(uint32_t sector, const ByteBuffer &data)
{
    if (!isMounted())
        RETURN_ERROR_AS_TRUE();

    if (_media->blockSize(sector) == 0 || _media->writeBlock(sector, data).is_error())
    {
        _status = DISK_STATUS_EOF;
        RETURN_ERROR_AS_TRUE();
    }
    _status = DISK_STATUS_OK;
    RETURN_SUCCESS_AS_FALSE();
}

uint8_t drivewireDisk::get_media_status()
{
    return isMounted() ? _status : DISK_STATUS_NO_MEDIA;
}

success_is_true drivewireDisk::write_blank(fnFile *f, const NewDiskRequest &req)
{
    uint8_t b[512];
    size_t n = req.numDisks * 315;

    Debug_printf("write_blank num_disks: %u\n", n);

    memset(b,0xFF,sizeof(b));

    for (size_t i=0;i<n;i++)
        fnio::fwrite(b,sizeof(b),1,f);

    RETURN_SUCCESS_AS_TRUE();
}


#endif /* BUILD_COCO */
