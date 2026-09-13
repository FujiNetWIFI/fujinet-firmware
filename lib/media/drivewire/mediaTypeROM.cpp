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

#if defined(PINMAP_FUJIVERSAL_DRIVEWIRE) || defined(COCO_HS_UART)
#define DBC_STREAM_ROM 0

// Pushes this object's already-mounted file (_media_fileh, _media_image_size)
// to the DBC device (the RP2040/RP2350 companion) as one
// NET_OPEN/NET_WRITE.../NET_CLOSE stream. Shared by mount() (automatic push,
// for boards wired that way) and fujiDevice's on-demand FUJI_PULL_ROM handler
// (push triggered later, e.g. by a One ROM-style companion's own knock/ack
// handshake) - see lib/media/rs232/diskTypeROM.cpp's push_stream() for the
// sibling implementation this mirrors.
bool MediaTypeROM::push_stream()
{
    struct { uint8_t id; u32le_t size; } open_hdr;
    static_assert(sizeof(open_hdr) == 5, "OPEN header must not be padded");
    open_hdr.id = DBC_STREAM_ROM;
    open_hdr.size = _media_image_size;

    auto reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_OPEN,
                                        std::string((const char *)&open_hdr, sizeof(open_hdr)));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("MediaTypeROM: failed to open DBC stream (%lu bytes)", (unsigned long)_media_image_size);
        return false;
    }

    fnio::fseek(_media_fileh, 0, SEEK_SET);

    bool ok = true;
    uint32_t sent = 0;
    size_t got;
    while ((got = fnio::fread(_media_blockbuff, 1, MEDIA_BLOCK_SIZE, _media_fileh)) > 0)
    {
        reply = SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_WRITE,
                                       std::string((char *)_media_blockbuff, got));
        if (!reply || reply->command() != CMD::FUJI_ACK)
        {
            Debug_printv("MediaTypeROM: failed to send DBC block at %lu of %lu bytes",
                         (unsigned long)sent, (unsigned long)_media_image_size);
            ok = false;
            break;
        }
        sent += got;
    }

    if (ok && sent != _media_image_size)
    {
        Debug_printv("MediaTypeROM: DBC stream short transfer: %lu of %lu bytes",
                     (unsigned long)sent, (unsigned long)_media_image_size);
        ok = false;
    }

    reply = ok ? SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE)
               : SYSTEM_BUS.sendCommand(FUJI_DEVICEID::DBC, CMD::NET_CLOSE, std::string(1, '\x01'));
    if (!reply || reply->command() != CMD::FUJI_ACK)
    {
        Debug_printv("MediaTypeROM: DBC close failed");
        return false;
    }

    Debug_printv("DBC stream complete: %lu / %lu bytes", (unsigned long)sent, (unsigned long)_media_image_size);
    return ok;
}
#endif /* PINMAP_FUJIVERSAL_DRIVEWIRE || COCO_HS_UART */

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

    if (!push_stream())
        return MEDIATYPE_UNKNOWN;

    return _mediatype;
#elif defined(COCO_HS_UART)
    // No auto-push here - a One ROM-style companion pulls the image itself,
    // on demand, via FUJI_PULL_ROM (drivewireFuji.cpp's fujicmd_pull_rom(),
    // which calls push_stream() directly) once its own knock/ack handshake
    // asks for it. The mount itself still succeeds: _mediatype is already
    // set above, which is what fujicmd_pull_rom() (and
    // fujicore_mount_disk_image_success()'s own MEDIATYPE_UNKNOWN-means-
    // failure check) actually key off.
    return _mediatype;
#else
    Debug_printv("ROM mount not supported on this FujiNet hardware.");
    return MEDIATYPE_UNKNOWN;
#endif
}

#endif // BUILD_COCO
