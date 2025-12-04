#ifndef DISK_H
#define DISK_H

#include "bus.h"
#include "media.h"

class drivewireDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;

public:
    drivewireDisk();
    ~drivewireDisk();

    mediatype_t disktype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };
    mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();

    bool write_blank(fnFile *f, uint8_t numDisks);

    bool read(uint32_t sector, uint8_t *buf);
    bool write(uint32_t sector, uint8_t *buf);

    void get_media_buffer(uint8_t **p_buffer, uint16_t *p_blk_size);
    uint8_t get_media_status();
};

#endif
