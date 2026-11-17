#ifndef _FUJI_DISK_
#define _FUJI_DISK_

#include "../device/disk.h"
#include "fujiHost.h"

#define MAX_DISPLAY_FILENAME_LEN 36

#define INVALID_HOST_SLOT 0xFF

class fujiDisk
{
public:
    disk_access_flags_t access_mode = DISK_ACCESS_MODE_READ;
    uint32_t disk_size = 0;
    fujiHost *host = nullptr;
    uint8_t host_slot = INVALID_HOST_SLOT;
    char filename[MAX_FILENAME_LEN] = { '\0' };
    DISK_DEVICE disk_dev;

    void reset();
    void reset(const char *filename, uint8_t hostslot, disk_access_flags_t access_mode);
};

#endif // _FUJI_DISK_
