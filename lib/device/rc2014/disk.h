#ifndef DISK_H
#define DISK_H

#include "bus.h"
#include "media.h"

#define STATUS_OK        0
#define STATUS_BAD_BLOCK 1
#define STATUS_NO_BLOCK  2
#define STATUS_NO_MEDIA  3
#define STATUS_NO_DRIVE  4

class rc2014Disk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
    TaskHandle_t diskTask;

    unsigned long blockNum=INVALID_SECTOR_VALUE;

    void read();
    void write(bool verify);
    void format();
    void status();
    void get_size();

    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    void rc2014_process(uint32_t commanddata, uint8_t checksum) override;

public:
    rc2014Disk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint32_t numBlocks);

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    bool device_active = false;

    ~rc2014Disk();
};

#endif /* s100_DISK_H */
