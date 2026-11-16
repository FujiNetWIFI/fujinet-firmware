#ifdef BUILD_MAC
#ifndef MAC_FLOPPY_H
#define MAC_FLOPPY_H

#include "FujiDiskDrive.h"
#include "../../media/mac/mediaTypeDCD.h"

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
class macFloppy : public FujiDiskDrive
{
protected:


    char disk_num = '0';
    bool enabled = false;
    int track_pos = 0;
    int old_pos = 0;
    int head_dir = 1;

    uint32_t _disk_size_in_blocks = 0;

    void dcd_status(uint8_t *buffer);

    // write capture from the Pico: raw IWM write bits, one frame per write
    uint8_t *_wcap = nullptr;
    size_t _wcap_len = 0;
    int _wcap_side = 0;
    bool _wcap_active = false;
    bool _wcap_overflow = false;
    void reload_track_buffers();
    void insert_floppy();

    // The Pico was told a disk is in ('s'/'d'), so unmount() may send 'r'
    bool _disk_inserted = false;
    int _archive_entry = 0;

    bool is_dcd_slot() { return disk_num >= '0' && disk_num < '0' + MAC_DCD_SLOTS; }
    bool is_floppy_slot() { return disk_num == '0' + MAC_FLOPPY_SLOT; }

public:
    bool readonly = true;

    macFloppy() {};
    ~macFloppy() {};

    error_is_true mount(FILE *f, const char *filename, uint32_t disksize,
                        disk_access_flags_t access_mode,
                        mediatype_t disk_type = MEDIATYPE_UNKNOWN);
    void unmount();
    struct NewDiskRequest {
        uint8_t hostSlot;
        uint8_t deviceSlot;
        char filename[MAX_FILENAME_LEN];
    };
    success_is_true write_blank(FILE *f, const NewDiskRequest &req) { RETURN_ERROR_AS_FALSE(); };
    int get_track_pos() { return track_pos; };
    void set_dir(int d) { head_dir = d; }
    int step();
    void change_track(int side);
    void update_track_buffers();
    void write_capture_data(const uint8_t *p, size_t n);
    void write_capture_end();
    void set_disk_number(char c) { disk_num = c; _devnum = c; }
    char get_disk_number() { return disk_num; };
    mediatype_t mediatype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    // Called from the bus service loop: flush an idle HD20 write cache
    void flush_if_idle();


    // Which disk image of an archive the next mount takes (1-based, 0 = the
    // first), and how many the mounted file holds
    void set_archive_entry(int entry) { _archive_entry = entry; }
    int image_count() { return _media != nullptr ? _media->image_count() : 0; }
    bool is_sector_image() { return _media != nullptr && _media->accepts_sector_writes(); }
    int num_sides() { return _media != nullptr ? _media->num_sides : 0; }
    uint32_t size_in_blocks() { return _media != nullptr ? _disk_size_in_blocks : 0; }
    // anything loaded in slots 1-4 is an HD20 by construction (see unmount())
    const MediaTypeDCD *dcd_media() { return _media != nullptr ? _media->dcd() : nullptr; }

    // Activity totals for the web UI (HD20: blocks; floppy: tracks loaded, sectors written)
    volatile uint32_t act_reads = 0;
    volatile uint32_t act_writes = 0;
    volatile uint32_t act_errors = 0;
    volatile uint32_t act_status = 0;
    volatile bool act_spinning = false;

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
    MediaType *_media = nullptr;

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
    mediatype_t disktype() { return _media == nullptr ? MEDIATYPE_UNKNOWN : _media->_mediatype; };

    ~iwmDisk2();
};

#endif
#endif /* BUILD_APPLE */
