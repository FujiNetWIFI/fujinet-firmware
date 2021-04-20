#ifndef _FUJI_DISK_
#define _FUJI_DISK_

#include <string>

#include "fujiHost.h"

#if defined( BUILD_ATARI )
#include "../sio/disk.h"
#include "mediaAtari.h"
#elif defined( BUILD_CBM )
#include "../iec/iecDisk.h"
#include "../media/mediaCBM.h"
#endif

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
    disktype_t disk_type = DISKTYPE_UNKNOWN;
    uint32_t disk_size = 0;
    fujiHost *host = nullptr;
    uint8_t host_slot = INVALID_HOST_SLOT;
    char filename[MAX_FILENAME_LEN] = { '\0' };

#if defined( BUILD_ATARI )
    sioDisk disk_dev;
#elif defined( BUILD_CBM )
    iecDisk disk_dev;
#endif

    void reset();
    void reset(const char *filename, uint8_t hostslot, uint8_t access_mode);
};


#endif // _FUJI_DISK_
