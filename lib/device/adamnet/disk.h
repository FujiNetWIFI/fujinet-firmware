#ifndef ADAM_DISK_H
#define ADAM_DISK_H

#include "bus.h"
#include "media.h"

class adamDisk : public adamNetDevice
{
private:
    MediaType *_media = nullptr;
    TaskHandle_t diskTask;

    unsigned long blockNum=0;

    void adamnet_control_status() override;
    void adamnet_control_ack();
    void adamnet_control_clr();
    void adamnet_control_receive();
    void adamnet_control_send();
    void adamnet_control_send_block_num();
    void adamnet_control_send_block_data();
    void adamnet_control_nack();
    void adamnet_response_status();
    void adamnet_response_ack();
    void adamnet_response_cancel();
    void adamnet_response_send();
    void adamnet_response_nack();
    void adamnet_control_ready();

    void adamnet_process(uint8_t b) override;

public:
    adamDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint32_t numBlocks);

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    bool device_active = false;

    ~adamDisk();
};

#endif /* ADAM_DISK_H */