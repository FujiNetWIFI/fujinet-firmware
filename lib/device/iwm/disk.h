#ifdef BUILD_APPLE
#ifndef DISK_H
#define DISK_H

#include "bus.h"
#include "media.h"

class iwmDisk : public iwmDevice
{
private:
    // temp device for disk image
    // todo: turn into FujiNet practice
    // get rid of this stuff by moving to correct locations after the prototype works
    struct device
    {
        FILE *sdf;
        //uint8_t device_id;          //to hold assigned device id's for the partitions
        unsigned long blocks;       //how many 512-byte blocks this image has
        unsigned int header_offset; //Some image files have headers, skip this many bytes to avoid them
        bool writeable;
    } d; // temporary device until have a disk device
    bool open_tnfs_image();
    bool open_image(std::string filename);

protected:
    void encode_status_reply_packet() override;
    void encode_extended_status_reply_packet() override;
    void encode_status_dib_reply_packet() override;
    void encode_extended_status_dib_reply_packet() override;

    MediaType *_disk = nullptr;

    //void iwm_read();
    //void iwm_write(bool verify);
    // void iwm_format();
    //void iwm_status(cmdPacket_t cmd); // override;
    void process(cmdPacket_t cmd) override; // uint32_t commanddata, uint8_t checksum); // override;

    void iwm_readblock(cmdPacket_t cmd) override;
    void iwm_writeblock(cmdPacket_t cmd) override;
    

    // void derive_percom_block(uint16_t numSectors);
    // void iwm_read_percom_block();
    // void iwm_write_percom_block();
    // void dump_percom_block();
    void shutdown() override; //todo change back

    char disk_num;

public:
    iwmDisk();
    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);
    bool write_blank(FILE *f, uint16_t numBlocks);

    void set_disk_number(char c) {disk_num = c;}

    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };
    void init();
    ~iwmDisk();
    virtual void startup_hack();
};

#endif
#endif /* BUILD_APPLE */