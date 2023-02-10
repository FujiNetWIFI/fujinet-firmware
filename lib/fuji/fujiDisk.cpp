#include "fujiDisk.h"

void fujiDisk::reset()
{
#ifdef DEVICE_TYPE
    host_slot = INVALID_HOST_SLOT;
    filename[0] = '\0';
    fileh = nullptr;
    access_mode = DISK_ACCESS_MODE_READ;
    disk_type = MEDIATYPE_UNKNOWN;
    host = nullptr;
#endif
}

void fujiDisk::reset(const char *fname, uint8_t hostslot, uint8_t mode)
{
#ifdef DEVICE_TYPE
    fileh = nullptr;
    disk_type = MEDIATYPE_UNKNOWN;
    host = nullptr;

    host_slot = hostslot;
    access_mode = mode;
#endif
}
