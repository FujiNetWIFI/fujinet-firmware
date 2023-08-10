#ifdef BUILD_COCO

#include "disk.h"

#include <cstring>

#include "../../include/debug.h"

#include "fuji.h"
#include "utils.h"

// External ref to fuji object.
extern drivewireFuji theFuji;

drivewireDisk::drivewireDisk()
{
    device_active = false;
}

// Destructor
drivewireDisk::~drivewireDisk()
{
}

// Process command
void drivewireDisk::drivewire_process(uint32_t commanddata, uint8_t checksum)
{
}

mediatype_t drivewireDisk::mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type)
{   
    return MEDIATYPE_UNKNOWN;
}
    
void drivewireDisk::unmount()
{
}

#endif /* BUILD_COCO */