#ifndef LYNX_DISK_H
#define LYNX_DISK_H

#include "bus.h"
#include "media.h"

#define STATUS_OK        0
#define STATUS_BAD_BLOCK 1
#define STATUS_NO_BLOCK  2
#define STATUS_NO_MEDIA  3
#define STATUS_NO_DRIVE  4


class lynxDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
    TaskHandle_t diskTask;

    unsigned long blockNum=INVALID_SECTOR_VALUE;

    void comlynx_control_clr();
    void comlynx_control_receive();
    void comlynx_control_send();
    void comlynx_control_send_block_num();
    void comlynx_control_send_block_data();
    virtual void comlynx_response_status() override;
    void comlynx_response_send();

    void comlynx_process(uint8_t b) override;

public:
    lynxDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint32_t numBlocks);
    virtual void reset() override;

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    ~lynxDisk();
};

#endif /* LYNX_DISK_H */