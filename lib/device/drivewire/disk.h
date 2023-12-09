#ifndef DISK_H
#define DISK_H

#include <fujiHost.h>
#include "bus.h"
#include "media.h"

class drivewireDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;

public:
    drivewireDisk();
    ~drivewireDisk();

    fujiHost *host = nullptr;

    mediatype_t disktype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
};

#endif
