#ifndef s100_DISK_H
#define s100_DISK_H

#include "bus.h"
#include "media.h"

#define STATUS_OK        0
#define STATUS_BAD_BLOCK 1
#define STATUS_NO_BLOCK  2
#define STATUS_NO_MEDIA  3
#define STATUS_NO_DRIVE  4

class s100spiDisk : public virtualDevice
{
private:
    MediaType *_media = nullptr;
    TaskHandle_t diskTask;

    unsigned long blockNum=INVALID_SECTOR_VALUE;

    void set_status(uint8_t s);
    void s100spi_control_clr();
    void s100spi_control_receive();
    void s100spi_control_send();
    void s100spi_control_send_block_num();
    void s100spi_control_send_block_data();
    virtual void s100spi_response_status();
    void s100spi_response_send();

    void s100spi_process(uint8_t b) override;

public:
    s100spiDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint32_t numBlocks);
    virtual void reset();

    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    bool device_active = false;

    ~s100spiDisk();
};

#endif /* s100_DISK_H */
