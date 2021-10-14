#ifndef ADAM_DISK_H
#define ADAM_DISK_H

#include "bus.h"
#include "media.h"

class adamDisk : public adamNetDevice
{
private:
    MediaType *_disk = nullptr;

    void adamnet_read();
    void adamnet_write();
    void adamnet_format();
    void adamnet_status() override;
    void adamnet_process(uint8_t b) override;

public:
    adamDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    mediatype_t mediatype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    bool device_active = false;

    ~adamDisk();
};

#endif /* ADAM_DISK_H */