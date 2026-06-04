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

    // Seek emulation state (see ADAMNET_DISK_SEEK_* in adamnet.h). Set at
    // block-number time; CONTROL.RECEIVE stays silent until _seek_deadline.
    // _seek_block + _last_blocknum_us distinguish a new request from the master's
    // rapid same-block retries so the timer isn't reset forever / shared between
    // blocks.
    int64_t _seek_deadline = 0;
    int64_t _last_blocknum_us = 0;
    unsigned long _seek_block = INVALID_SECTOR_VALUE;
    // Set true once a CONTROL.RECEIVE confirms the pending block# is a READ. The
    // seek STATUS-silence applies only to reads; a write must keep STATUS answered
    // or the master abandons the block before sending its data.
    bool _seek_is_read = false;

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
    error_is_true write_blank(FILE *f, uint32_t numBlocks);
    virtual void reset() override;
    MediaType *get_media() { return  _media; }
    void set_media(MediaType *__media) { _media = __media; }

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    ~adamDisk();
};

#endif /* ADAM_DISK_H */
