#include "fujiDisk.h"

void fujiDisk::reset()
{
    host_slot = INVALID_HOST_SLOT;
    filename[0] = '\0';
    fileh = nullptr;
    access_mode = DISK_ACCESS_MODE_READ;
    disk_type = MEDIA_TYPE_UNKNOWN;
    host = nullptr;
}

void fujiDisk::reset(const char *fname, uint8_t hostslot, uint8_t mode)
{
    fileh = nullptr;
    disk_type = MEDIA_TYPE_UNKNOWN;
    host = nullptr;

    host_slot = hostslot;
    access_mode = mode;
}
