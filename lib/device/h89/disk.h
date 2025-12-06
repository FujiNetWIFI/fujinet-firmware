#ifndef DISK_H
#define DISK_H

#include "bus.h"
#include "media.h"

#define STATUS_OK        0
#define STATUS_BAD_BLOCK 1
#define STATUS_NO_BLOCK  2
#define STATUS_NO_MEDIA  3
#define STATUS_NO_DRIVE  4

class H89Disk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
    void process(uint32_t commanddata, uint8_t checksum) override;

public:
    H89Disk();

    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();

    bool device_active = false;

    ~H89Disk();
};

#endif /* s100_DISK_H */
