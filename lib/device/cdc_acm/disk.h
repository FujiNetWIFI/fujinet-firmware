#ifndef DISK_H
#define DISK_H

#include <stdio.h>

#include "bus.h"
#include "media.h"

class cdcDisk : public virtualDevice
{
public:
    cdcDisk() {}
    ~cdcDisk() {}

    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN) { return MEDIATYPE_UNKNOWN; }
    void unmount() {}
};

#endif /* DISK_H */
