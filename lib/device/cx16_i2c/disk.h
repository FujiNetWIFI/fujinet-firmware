#ifndef DISK_H
#define DISK_H

#include <fujiHost.h>
#include "bus.h"
#include "media.h"

class cx16Disk : public virtualDevice
{
private:
    MediaType *_disk = nullptr;

    void sio_read();
    void sio_write(bool verify);
    void sio_format();
    void status() override;
    void process(uint32_t commanddata, uint8_t checksum) override;

public:
    cx16Disk();
    fujiHost *host;
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    ~cx16Disk();
};

#endif
