#ifndef ADAM_DISK_H
#define ADAM_DISK_H

#include "../disk.h"
#include "bus.h"
#include "media.h"

#define STATUS_OK        0
#define STATUS_BAD_BLOCK 1
#define STATUS_NO_BLOCK  2
#define STATUS_NO_MEDIA  3
#define STATUS_NO_DRIVE  4

class adamDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
    TaskHandle_t diskTask;

    unsigned long blockNum=INVALID_SECTOR_VALUE;

    void adamnet_control_clr();
    void adamnet_control_receive();
    void adamnet_control_send();
    void adamnet_control_send_block_num();
    void adamnet_control_send_block_data();
    virtual void adamnet_response_status() override;
    void adamnet_response_send();

    void adamnet_process(uint8_t b) override;

public:
    adamDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint32_t numBlocks);
    virtual void reset() override;
    MediaType *get_media() { return  _media; }
    void set_media(MediaType *__media) { _media = __media; }

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    ~adamDisk();
};

#endif /* ADAM_DISK_H */
