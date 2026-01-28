#ifdef BUILD_H89

#include "disk.h"

#include <memory.h>
#include <string.h>

#include "../../include/debug.h"

#include "media.h"
#include "utils.h"

H89Disk::H89Disk()
{
}

// Destructor
H89Disk::~H89Disk()
{
}

mediatype_t H89Disk::mount(FILE *f, const char *filename, uint32_t disksize,
                           disk_access_flags_t access_mode, mediatype_t disk_type)
{
    mediatype_t mt = MEDIATYPE_UNKNOWN;

    Debug_printf("disk MOUNT %s\n", filename);

    // Destroy any existing MediaType
    if (_media != nullptr)
    {
        delete _media;
        _media = nullptr;
    }

    // Determine MediaType based on filename extension
    if (disk_type == MEDIATYPE_UNKNOWN && filename != nullptr)
        disk_type = MediaType::discover_mediatype(filename, disksize);

    if (disk_type != MEDIATYPE_UNKNOWN) {
        _media = new MediaTypeIMG();
        mt = _media->mount(f, disksize, disk_type);
        device_active = true;
        Debug_printf("disk MOUNT mediatype = %d: active\n", disk_type);
    } else {
        device_active = false;
        Debug_printf("disk MOUNT unknown: deactive\n");
    }

    return mt;
}

void H89Disk::unmount()
{
    Debug_print("disk UNMOUNT\n");

    if (_media != nullptr) {
        _media->unmount();
        device_active = false;
    }
}

void H89Disk::process(uint32_t commanddata, uint8_t checksum)
{
}

#endif /* NEW_TARGET */
