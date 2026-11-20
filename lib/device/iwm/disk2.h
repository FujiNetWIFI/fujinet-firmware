#ifdef BUILD_APPLE
#ifndef DISK2_H
#define DISK2_H

#include <atomic>

#include "disk.h"
#include "../media/media.h"

class iwmDisk2 : public iwmDisk
{

protected:
    int track_pos;
    int old_pos;
    uint8_t oldphases;

    std::atomic<bool> _media_ready{false};
    std::atomic<int> _media_users{0};

    void retire_media();
    void copy_track_from_media(int indicator);

public:
    iwmDisk2();
    void init();
    virtual mediatype_t mount_file(fnFile *f, uint32_t disksize, mediatype_t disk_type) override;
    error_is_true write_blank(fnFile *f, uint16_t sectorSize, uint16_t numSectors);
    void unmount() override;
    int get_track_pos() { return track_pos; };
    bool phases_valid(uint8_t phases);
    success_is_true move_head();
    void change_track(int indicator);

    error_is_true write_sector(int track, int sector, uint8_t* buffer);

    ~iwmDisk2();
};

#endif
#endif /* BUILD_APPLE */
