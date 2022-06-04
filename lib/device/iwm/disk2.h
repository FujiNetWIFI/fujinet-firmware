#ifdef BUILD_APPLE
#ifndef DISK2_H
#define DISK2_H

#include "bus.h"
#include "media.h"

class iwmDisk2 : public iwmDevice
{

protected:
    MediaType *_disk = nullptr;

    // unused because not a smartport device
    void encode_status_reply_packet() override {};
    void encode_extended_status_reply_packet() override {};
    void encode_status_dib_reply_packet() override {};
    void encode_extended_status_dib_reply_packet() override {};
    void process(cmdPacket_t cmd) override {}; 
    void iwm_readblock(cmdPacket_t cmd) override {};
    void iwm_writeblock(cmdPacket_t cmd) override {};
   
    void shutdown() override;
    char disk_num;

public:
    iwmDisk2();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);
    bool write_blank(FILE *f, uint16_t numBlocks);

    void set_disk_number(char c) { disk_num = c; }
    char get_disk_number() { return disk_num; };
    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };
    // void init();
    ~iwmDisk2();
};

#endif
#endif /* BUILD_APPLE */