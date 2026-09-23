#ifdef BUILD_MAC
#ifndef MAC_FLOPPY_H
#define MAC_FLOPPY_H

#include <memory>

#include "../disk.h"
#include "bus.h"
#include "../media/media.h"
#include "../../media/mac/sitMount.h"

/*
// drive state bits
#define STAT_DIRTN   0b0000
#define STAT_STEP    0b0001
#define STAT_MOTORON 0b0010
#define STAT_EJECT   0b0011
#define STAT_DATAHD0 0b0100
        not assigned 0b0101
#define STAT_SS      0b0110
#define STAT_DRVIN   0b0111
#define STAT_CSTIN   0b1000
#define STAT_WRTPRT  0b1001
#define STAT_TKO     0b1010
#define STAT_TACH    0b1011
#define STAT_DATAHD1 0b1100
        not assigned 0b1101
#define STAT_READY   0b1110
#define STAT_REVISED 0b1111
 */

/**
 * One Mac disk slot. Depending on the slot number and the image type this
 * behaves either as an HD20 (DCD) hard disk (slots 0..3, .dsk/.image) or
 * as the 800K GCR floppy (slot 4, .moof).
 */
class macFloppy : public virtualDevice
{
protected:
    MediaType *_disk = nullptr;

    // Set while the mounted image came from an archive; owns its PSRAM copy
    std::unique_ptr<SitMount> _sit;

    char disk_num = '0';
    bool enabled = false;
    int track_pos = 0;
    int old_pos = 0;
    int head_dir = 1;

    uint32_t _disk_size_in_blocks = 0;

    void dcd_status(uint8_t *buffer);

    // write capture from the Pico: raw IWM write bits, one frame per write
    bool _sector_image = false;   // slot 5 holds a sector image (writable kind)
    uint8_t *_wcap = nullptr;
    size_t _wcap_len = 0;
    int _wcap_side = 0;
    bool _wcap_active = false;
    bool _wcap_overflow = false;
    void reload_track_buffers();

    // The Pico was told a disk is in ('s'/'d'), so unmount() may send 'r'
    bool _disk_inserted = false;

    bool is_dcd_slot() { return disk_num >= '0' && disk_num < '0' + MAC_DCD_SLOTS; }
    bool is_floppy_slot() { return disk_num == '0' + MAC_FLOPPY_SLOT; }

public:
    bool readonly = true;

    macFloppy() {};
    ~macFloppy() {};

    mediatype_t mount(FILE *f, const char *filename, uint32_t disksize,
                      disk_access_flags_t access_mode,
                      mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors) { return false; };
    int get_track_pos() { return track_pos; };
    void set_dir(int d) { head_dir = d; }
    int step();
    void change_track(int side);
    void update_track_buffers();
    void write_capture_data(const uint8_t *p, size_t n);
    void write_capture_end();
    void set_disk_number(char c) { disk_num = c; _devnum = c; }
    char get_disk_number() { return disk_num; };
    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    // Archive-backed mount details, for the web UI and /sitdownload
    bool has_sit_source() { return _sit != nullptr; }
    const char *sit_inner_filename() { return (_sit != nullptr) ? _sit->inner_filename : ""; }
    uint32_t sit_image_len() { return (_sit != nullptr) ? _sit->image_len : 0; }
    const uint8_t *sit_image_data() { return (_sit != nullptr) ? _sit->image_buf : nullptr; }
    const char *sit_archive_kind() { return (_sit != nullptr) ? _sit->archive_kind : ""; }
    const char *sit_method_name() { return (_sit != nullptr) ? _sit->method_name : ""; }
    bool sit_was_ndif() { return (_sit != nullptr) && _sit->was_ndif; }

    void shutdown() override {};
    void process(mac_cmd_t cmd) override;
};

#endif // MAC_FLOPPY_H
#endif // BUILD_MAC

#if 0
#ifndef DISK2_H
#define DISK2_H

#include "bus.h"
#include "../media/media.h"

class iwmDisk2 : public iwmDevice
{

protected:
    MediaType *_disk = nullptr;

    // unused because not a smartport device
    void send_status_reply_packet() override{};
    void send_extended_status_reply_packet() override{};
    void send_status_dib_reply_packet() override{};
    void send_extended_status_dib_reply_packet() override{};
    void process(iwm_decoded_cmd_t cmd) override{};
    void iwm_readblock(iwm_decoded_cmd_t cmd) override{};
    void iwm_writeblock(iwm_decoded_cmd_t cmd) override{};

    void shutdown() override;
    char disk_num;
    bool enabled;
    int track_pos;
    int old_pos;
    uint8_t oldphases;

public:
    iwmDisk2();
    void init();
    mediatype_t mount(FILE *f, mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    bool write_blank(FILE *f, uint16_t sectorSize, uint16_t numSectors);
    int get_track_pos() { return track_pos; };
    bool phases_valid(uint8_t phases);
    bool move_head();
    void change_track(int indicator);

    // void set_disk_number(char c) { disk_num = c; }
    // char get_disk_number() { return disk_num; };
    mediatype_t disktype() { return _disk == nullptr ? MEDIATYPE_UNKNOWN : _disk->_mediatype; };

    ~iwmDisk2();
};

#endif
#endif /* BUILD_APPLE */