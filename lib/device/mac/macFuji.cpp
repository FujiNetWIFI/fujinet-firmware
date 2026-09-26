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

// Strips a trailing "#N" from filename and returns N, or 0 if there is none
static int split_archive_entry(char *filename)
{
    char *hash = strrchr(filename, '#');
    if (hash == nullptr || hash[1] == '\0' || strspn(hash + 1, "0123456789") != strlen(hash + 1))
        return 0;
    int entry = atoi(hash + 1);
    *hash = '\0';
    return entry;
}

success_is_true macFuji::fujicore_mount_disk_image_success(uint8_t deviceSlot,
                                                           disk_access_flags_t access_mode)
{
    if (!validate_device_slot(deviceSlot))
        RETURN_ERROR_AS_FALSE();

    fujiDisk &disk = _fnDisks[deviceSlot];
    int entry = split_archive_entry(disk.filename);
    std::string path = disk.filename; // before the base prepends the host prefix
    disk.disk_dev.set_archive_entry(entry);

    success_is_true mounted = fujiDevice::fujicore_mount_disk_image_success(deviceSlot, access_mode);
    if (entry > 0)
    {
        size_t len = strlen(disk.filename);
        snprintf(disk.filename + len, sizeof(disk.filename) - len, "#%d", entry);
    }
    if (mounted.is_error())
        RETURN_ERROR_AS_FALSE();

    if (entry == 0 && disk.disk_dev.image_count() > 1)
        fan_out_archive(deviceSlot, path, access_mode);
    RETURN_SUCCESS_AS_TRUE();
}

// Mounts images 2..N of the archive in deviceSlot into the empty slots right
// after it, up to the floppy slot. Stops at a taken slot, so a Mount All finds
// the slots it filled earlier already configured, or when an image does not
// mount (no PSRAM left, or it cannot go in that slot).
void macFuji::fan_out_archive(uint8_t deviceSlot, const std::string &path, disk_access_flags_t mode)
{
    uint8_t host_slot = _fnDisks[deviceSlot].host_slot;
    int images = _fnDisks[deviceSlot].disk_dev.image_count();
    int slot = deviceSlot + 1;

    for (int entry = 2; entry <= images && slot <= MAC_FLOPPY_SLOT; entry++, slot++)
    {
        fujiDisk &disk = _fnDisks[slot];
        if (disk.host_slot != INVALID_HOST_SLOT)
            break;

        std::string name = path + "#" + std::to_string(entry);
        disk.reset(name.c_str(), host_slot, mode);
        strlcpy(disk.filename, name.c_str(), sizeof(disk.filename));
        if (fujicore_mount_disk_image_success(slot, mode).is_error())
        {
            Debug_printf("\nArchive: image %d of %d did not mount in slot %d, stopping\n", entry, images, slot + 1);
            disk.reset();
            break;
        }
        Config.store_mount(slot, host_slot, name.c_str(),
                           (mode & DISK_ACCESS_MODE_WRITE) ? fnConfig::mount_modes::MOUNTMODE_WRITE
                                                           : fnConfig::mount_modes::MOUNTMODE_READ);
    }
    Config.save();
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
