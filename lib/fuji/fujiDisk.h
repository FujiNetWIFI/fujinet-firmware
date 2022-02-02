#ifndef _FUJI_DISK_
#define _FUJI_DISK_

#include "../device/disk.h"

#include "fujiHost.h"

#define MAX_DISPLAY_FILENAME_LEN 36
#define MAX_FILENAME_LEN 256

#define DISK_ACCESS_MODE_READ 1
#define DISK_ACCESS_MODE_WRITE 2
#define DISK_ACCESS_MODE_FETCH 128

#define INVALID_HOST_SLOT 0xFF

class fujiDisk
{
public:    
    FILE* fileh = nullptr;
    uint8_t access_mode = DISK_ACCESS_MODE_READ;
    MEDIA_TYPE disk_type = MEDIA_TYPE_UNKNOWN;
    uint32_t disk_size = 0;
    fujiHost *host = nullptr;
    uint8_t host_slot = INVALID_HOST_SLOT;
    char filename[MAX_FILENAME_LEN] = { '\0' };
    DEVICE_TYPE disk_dev;

    void reset();
    void reset(const char *filename, uint8_t hostslot, uint8_t access_mode);
};


#endif // _FUJI_DISK_
