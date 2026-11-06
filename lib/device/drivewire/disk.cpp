#ifdef BUILD_COCO

#include "disk.h"
#include "rom/ROMFactory.h"

#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "bus.h"
#include "fujiCommandID.h"
#include "fujiDevice.h"
#include "utils.h"

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

    if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN, (uint16_t)0))
    {
        Debug_printv("Failed to open pico bank");
        RETURN_ERROR_AS_TRUE();
    }

    ByteBuffer blockbuff;
    size_t sent = 0;
    while (true)
    {
        auto got = rom.read(blockbuff, MEDIA_BLOCK_SIZE);
        if (got.is_error() || got == 0)
        {
            if (got.is_error())
                Debug_printv("ROM read short: sent %lu of %lu bytes",
                             (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE, blockbuff))
        {
            Debug_printv("Failed to send ROM block at %lu of %lu bytes", (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        sent += got;
    }

    SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
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

// Destructor
drivewireDisk::~drivewireDisk()
{
}

mediatype_t drivewireDisk::mount(fnFile *f, const char *filename, uint32_t disksize,
                                 disk_access_flags_t access_mode, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("DW disk MOUNT %s\n", filename);

    // Destroy any existing MediaType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename);

    switch (disk_type)
    {
    case MEDIATYPE_DSK:
        _media = new MediaTypeDSK();
        break;
    case MEDIATYPE_MRM:
        _media = new MediaTypeMRM();
        break;
    case MEDIATYPE_VDK:
        _media = new MediaTypeVDK();
        break;
    case MEDIATYPE_ROM:
    {
        device_active = false;
        auto rom = ROMFactory::open<
            CoCoCartROM
            >(filename, f, nullptr);
        if (!rom)
        {
            fnio::fclose(f);
            return MEDIATYPE_UNKNOWN;
        }

        error_is_true pushed = push_rom_image(*rom);
        rom->close();
        return pushed.is_success() ? MEDIATYPE_ROM : MEDIATYPE_UNKNOWN;
    }
    case MEDIATYPE_CAS:
        _media = new MediaTypeCASDSK();
        break;
    default:
        device_active = false;
        break;
    }

    if (_media)
    {
        strcpy(_media->_disk_filename,filename);
        _media->_media_read_only = !(access_mode & DISK_ACCESS_MODE_WRITE);
        mt = _media->mount(f, disksize);
        if (mt == MEDIATYPE_UNKNOWN)
        {
            delete _media;
            _media = nullptr;
            device_active = false;
        }
        else
        {
            device_active = true;
        }
    }

    return mt;
}

void drivewireDisk::unmount()
{
    Debug_print("DW disk UNMOUNT\n");

    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }
    device_active = false;
}

void drivewireDisk::set_media_host(fujiHost *host)
{
    if (_media)
        _media->_media_host = host;
}

error_is_true drivewireDisk::read(uint32_t lsn, uint8_t *buf)
{
    if (_media == nullptr)
        RETURN_ERROR_AS_TRUE();

    bool r = _media->read(lsn,0);
    // copy data to destination buffer, if provided
    if (buf)
    {
        memcpy(buf, _media->_media_blockbuff, MEDIA_BLOCK_SIZE);
    }
    RETURN_ERROR_IF(r);
}

error_is_true drivewireDisk::write(uint32_t lsn, uint8_t *buf)
{
    if (!buf || _media == nullptr)
    {
        Debug_printv("No buffer or media, IGNORED.");
        RETURN_ERROR_AS_TRUE();
    }

    memcpy(_media->_media_blockbuff,buf,MEDIA_BLOCK_SIZE);
    bool r = _media->write(lsn,0);

    RETURN_ERROR_IF(r);
}

void drivewireDisk::get_media_buffer(uint8_t **p_buffer, uint16_t *p_blk_size)
{
    if (_media)
    {
        // query media for block buffer and block size
        _media->get_block_buffer(p_buffer, p_blk_size);
    }
    else
    {
        *p_buffer = nullptr;
        *p_blk_size = 0;
    }
}

uint8_t drivewireDisk::get_media_status()
{
    if (!_media)
        return 3; // ? NO MEDIA
    return _media->status();
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
