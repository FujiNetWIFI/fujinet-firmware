#ifndef _FUJI_DISK_
#define _FUJI_DISK_

#include "../device/disk.h"

// #ifdef BUILD_APPLE
// #include "../device/iwm/disk.h"
// #define MEDIA_TYPE mediatype_t
// #define MEDIA_TYPE_UNKNOWN MEDIATYPE_UNKNOWN
// #define DEVICE_TYPE iwmDisk
// #endif

#include "fujiHost.h"

#define MAX_DISPLAY_FILENAME_LEN 36
#define MAX_FILENAME_LEN 256

#define DISK_ACCESS_MODE_READ    0x01
#define DISK_ACCESS_MODE_WRITE   0x02
#define DISK_ACCESS_MODE_MOUNTED 0x40

#define INVALID_HOST_SLOT 0xFF

class fujiDisk
{
public:    
    fnFile* fileh = nullptr;
    uint8_t access_mode = DISK_ACCESS_MODE_READ;
    mediatype_t disk_type = MEDIATYPE_UNKNOWN;
    uint32_t disk_size = 0;
    fujiHost *host = nullptr;
    uint8_t host_slot = INVALID_HOST_SLOT;
    char filename[MAX_FILENAME_LEN] = { '\0' };
    DEVICE_TYPE disk_dev;

    void reset();
    void reset(const char *filename, uint8_t hostslot, uint8_t access_mode);
};


#endif // _FUJI_DISK_
