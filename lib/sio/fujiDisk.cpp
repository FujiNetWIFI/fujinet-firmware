#include "fujiDisk.h"

#include <string.h>

void fujiDisk::reset()
{
    hostidx = INVALID_HOST_IDX;
    filename[0] = '\0';
    file = nullptr;
    access_mode = DISK_ACCESS_MODE_READ;
    disk_type = DISKTYPE_UNKNOWN;
    host = nullptr;
}

void fujiDisk::reset(const char *fname, uint8_t hostslot, uint8_t mode)
{
    file = nullptr;
    disk_type = DISKTYPE_UNKNOWN;
    host = nullptr;

    hostidx = hostslot;
    access_mode = mode;
    strlcpy(filename, fname, sizeof(filename));
}
