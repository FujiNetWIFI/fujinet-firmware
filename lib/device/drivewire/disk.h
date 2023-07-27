#ifndef DISK_H
#define DISK_H

#include <fujiHost.h>
#include "bus.h"
#include "media.h"

class drivewireDisk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;

    void drivewire_process(uint32_t commanddata, uint8_t checksum);

public:
    drivewireDisk();
    ~drivewireDisk();

    fujiHost *host = nullptr;

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
};

#endif
