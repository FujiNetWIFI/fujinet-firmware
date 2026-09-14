#ifdef BUILD_MAC

#include "macFuji.h"

#include "fnSystem.h"
#include "fnConfig.h"
#include "led.h"
#include "fnWiFi.h"
#include "utils.h"
#include "string_utils.h"

#include <cstring>
#include <string>

#define IMAGE_EXTENSION ".dsk"

macFuji platformFuji;
fujiDevice *theFuji = &platformFuji; // Global fuji object.

macFuji::macFuji() : fujiDevice(MAX_DISK_DEVICES, IMAGE_EXTENSION, std::nullopt)
{
    Debug_printf("Announcing the MacFuji!!!\n");
}

// Initializes base settings and adds our devices to the bus
void macFuji::setup()
{
    populate_slots_from_config();

    // There is no CONFIG disk on the Mac; the web UI is the config.
    boot_config = false;

    // Slots 0..3 answer as HD20/DCD drives '0'..'3', slot 4 is the floppy.
    // The Pico protocol identifies drives by these single characters.
    for (int i = 0; i < MAX_DISK_DEVICES; i++)
    {
        _fnDisks[i].disk_dev.set_disk_number('0' + i);
        SYSTEM_BUS.addDevice(&_fnDisks[i].disk_dev, FUJI_DEVICEID::DISK + i);
    }

    SYSTEM_BUS.addDevice(this, FUJI_DEVICEID::FUJINET);
}

size_t macFuji::set_additional_direntry_details(fsdir_entry_t *f, uint8_t *dest,
                                                uint8_t maxlen)
{
    struct {
        dirEntryTimestamp modified;
        uint32_t size;
        uint8_t is_dir;
        uint8_t is_trunc;
        uint8_t mediatype;
    } __attribute__((packed)) custom_details;
    dirEntryDetails details;

    details = _additional_direntry_details(f);
    custom_details.modified = details.modified;
    custom_details.modified.year -= 100;
    custom_details.size = htole32(details.size);
    custom_details.is_dir = details.flags & DET_FF_DIR;
    custom_details.mediatype = details.mediatype;

    maxlen -= sizeof(custom_details);
    // Subtract a byte for a terminating slash on directories
    if (custom_details.is_dir)
        maxlen--;

    custom_details.is_trunc = strlen(f->filename) >= maxlen ? DET_FF_TRUNC : 0;
    memcpy(dest, &custom_details, sizeof(custom_details));
    return sizeof(custom_details);
}

#endif // BUILD_MAC
