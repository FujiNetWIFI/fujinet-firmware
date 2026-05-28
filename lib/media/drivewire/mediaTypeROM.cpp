#ifdef BUILD_COCO

#include "mediaTypeROM.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "../../include/debug.h"

#include "bus.h"
#include "fujiCommandID.h"

error_is_true MediaTypeROM::read(uint32_t blockNum, uint16_t *readcount)
{
    Debug_printf("DW ROM READ not supported\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::write(uint32_t blockNum, bool verify)
{
    Debug_printf("DW ROM WRITE not supported\n");
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeROM::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

uint8_t MediaTypeROM::status()
{
    return 2;
}

mediatype_t MediaTypeROM::mount(fnFile *f, uint32_t disksize)
{
    Debug_printf("DW ROM MOUNT %s (%lu bytes)\n", _disk_filename, (unsigned long)disksize);

    _media_fileh = f;
    _mediatype = MEDIATYPE_ROM;
    _media_image_size = disksize;

#ifdef PINMAP_FUJIVERSAL_DRIVEWIRE
    if (SYSTEM_BUS.isBoIP())
    {
        Debug_printv("ROM media requires a real pico; not supported over BoIP");
        return MEDIATYPE_UNKNOWN;
    }

    if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_OPEN, (uint16_t)0))
    {
        Debug_printv("Failed to open pico bank");
        return MEDIATYPE_UNKNOWN;
    }

    fnio::fseek(_media_fileh, 0, SEEK_SET);

    uint32_t sent = 0;
    while (sent < disksize)
    {
        size_t want = (disksize - sent) > MEDIA_BLOCK_SIZE ? MEDIA_BLOCK_SIZE : (disksize - sent);
        size_t got = fnio::fread(_media_blockbuff, 1, want, _media_fileh);
        if (got == 0)
        {
            Debug_printv("ROM read short: sent %lu of %lu bytes", (unsigned long)sent, (unsigned long)disksize);
            break;
        }
        if (!SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_WRITE,
                                    std::string((char *)_media_blockbuff, got)))
        {
            Debug_printv("Failed to send ROM block at %lu of %lu bytes", (unsigned long)sent, (unsigned long)disksize);
            break;
        }
        sent += got;
    }

    SYSTEM_BUS.sendCommand(FUJI_DEVICEID_DBC, NETCMD_CLOSE);
    Debug_printv("ROM transfer complete: %lu / %lu bytes", (unsigned long)sent, (unsigned long)disksize);

    return _mediatype;
#else
    Debug_printv("ROM mount not supported on this FujiNet hardware.");
    return MEDIATYPE_UNKNOWN;
#endif
}

#endif // BUILD_COCO
