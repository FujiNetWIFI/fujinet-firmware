#ifdef BUILD_COCO

#include "disk.h"

#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "bus.h"
#include "fujiCommandID.h"
#include "fujiDevice.h"
#include "utils.h"

// A ROM image isn't served block by block the way a disk is: the whole file
// goes to the cartridge's RP2040 at mount time, which presents it to the CoCo
// as cartridge ROM. Nothing reads it back through the media object afterwards.
static bool push_rom_image(MediaTypeROM *rom)
{
#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
    if (SYSTEM_BUS.isBoIP())
    {
        Debug_printv("ROM media requires a real pico; not supported over BoIP");
        return false;
    }

    uint32_t imagesize = rom->stream_size();
    if (imagesize == 0)
    {
        Debug_printv("ROM image is empty or unreadable");
        return false;
    }

    if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN, (uint16_t)0))
    {
        Debug_printv("Failed to open pico bank");
        return false;
    }

    uint8_t blockbuff[MEDIA_BLOCK_SIZE];
    uint32_t sent = 0;
    while (sent < imagesize)
    {
        size_t want = (imagesize - sent) > MEDIA_BLOCK_SIZE ? MEDIA_BLOCK_SIZE : (imagesize - sent);
        size_t got = rom->stream_read(sent, blockbuff, want);
        if (got == 0)
        {
            Debug_printv("ROM read short: sent %lu of %lu bytes", (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                    std::string((char *)blockbuff, got)))
        {
            Debug_printv("Failed to send ROM block at %lu of %lu bytes", (unsigned long)sent, (unsigned long)imagesize);
            break;
        }
        sent += got;
    }

    SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE);
    Debug_printv("ROM transfer complete: %lu / %lu bytes", (unsigned long)sent, (unsigned long)imagesize);

    return sent == imagesize;
#else
    (void)rom;
    Debug_printv("ROM mount not supported on this FujiNet hardware.");
    return false;
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
        MediaTypeROM *rom = new MediaTypeROM();
        _media = rom;
        strcpy(_media->_disk_filename, filename);
        _media->_media_read_only = !(access_mode & DISK_ACCESS_MODE_WRITE);
        mt = _media->mount(f, disksize);
        if (mt == MEDIATYPE_ROM && !push_rom_image(rom))
            mt = MEDIATYPE_UNKNOWN;
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
        return mt;
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
}

void drivewireDisk::set_media_host(fujiHost *host)
{
    if (_media)
        _media->_media_host = host;
}

error_is_true drivewireDisk::read(uint32_t lsn, uint8_t *buf)
{
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
    if (!buf)
    {
        Debug_printv("BUFFER is NULL, IGNORED.");
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

success_is_true drivewireDisk::write_blank(fnFile *f, uint8_t numDisks)
{
    uint8_t b[512];
    size_t n = numDisks * 315;

    Debug_printf("write_blank num_disks: %u\n", n);

    memset(b,0xFF,sizeof(b));

    for (size_t i=0;i<n;i++)
        fnio::fwrite(b,sizeof(b),1,f);

    RETURN_SUCCESS_AS_TRUE();
}


#endif /* BUILD_COCO */
