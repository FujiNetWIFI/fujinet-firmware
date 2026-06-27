#ifdef BUILD_APPLE
#ifndef DISK_H
#define DISK_H

#include "../disk.h"
#include "bus.h"
#include "../media/media.h"

class iwmDisk : public virtualDevice
{
private:
    spError_t err_result = SP_ERR::NOERROR;
    void prodos_encode_datetime(unsigned short *date_out, unsigned short *time_out);
    int prodos_write_block(fnFile *f, const unsigned char *buf);
    error_is_true prodos_write_boot_block(fnFile *f);
    error_is_true prodos_write_sos_block(fnFile *f);
    error_is_true prodos_write_directory_sectors(fnFile *f, uint16_t numBlocks, const char *label = nullptr);
    error_is_true prodos_write_bitmap(fnFile *f, uint16_t numBlocks);
    error_is_true prodos_write_data_blocks(fnFile *f, uint16_t numBlocks);

protected:
    void send_status_reply_packet() override;
    void send_extended_status_reply_packet() override;
    iwm_device_info_block_t create_dib_reply_packet() override;

    MediaType *_disk = nullptr;

    void iwm_ctrl(iwm_decoded_cmd_t cmd) override;
    void iwm_readblock(iwm_decoded_cmd_t cmd) override;
    void iwm_writeblock(iwm_decoded_cmd_t cmd) override;
    void iwm_format(iwm_decoded_cmd_t cmd) override;

    void shutdown() override; //todo change back

    char disk_num;

    /* Determine smartport type based on # of blocks */
    uint8_t smartport_device_type();
    uint8_t smartport_device_subtype();

    uint8_t create_status();
    std::vector<uint8_t> create_blocksize(bool is_32_bits = false);

public:
    iwmDisk();
    mediatype_t mount(fnFile *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    virtual mediatype_t mount_file(fnFile *f, uint32_t disksize, mediatype_t disk_type);
    void unmount();
    error_is_true write_blank(fnFile *f, uint16_t numBlocks, uint8_t blank_header_type);

    void set_disk_number(char c) { disk_num = c; }
    char get_disk_number() { return disk_num; };
    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };
    // void init();
    ~iwmDisk();
    // virtual void startup_hack();
};

#endif /* DISK_H */
#endif /* BUILD_APPLE */
