#include "fujiDisk.h"

void fujiDisk::reset()
{
#ifdef DISK_DEVICE
    host_slot = INVALID_HOST_SLOT;
    filename[0] = '\0';
    access_mode = DISK_ACCESS_MODE_READ;
    host = nullptr;
#endif
}

void fujiDisk::reset(const char *fname, uint8_t hostslot, disk_access_flags_t mode)
{
#ifdef DISK_DEVICE
    host = nullptr;

    host_slot = hostslot;
    access_mode = (disk_access_flags_t)(mode & ~DISK_ACCESS_MODE_MOUNTED);
#endif
}
