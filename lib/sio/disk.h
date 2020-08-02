#ifndef DISK_H
#define DISK_H

#include "sio.h"
#include "diskType.h"

extern int command_frame_counter;

// Specify sectors 0-UNCACHED_REGION to not be cached
// Normally, this is set to 3 to not cache the first three sectors, which
// may be only 128 bytes compared to 256 bytes for double density images.
// If you set this to 65535, then the cache is completely disabled.
// Please do not change this value yet, the cache has been ripped out
// due to be rewritten.
#define UNCACHED_REGION 65535

class sioDisk : public sioDevice
{
private:
    FILE *_file;
    disktype_t _disktype = DISKTYPE_UNKNOWN;

    uint16_t _sectorSize = 128;
    uint8_t _sector[256];

    uint8_t _sectorCache[4][256];
    uint16_t _lastSectorNum = 65535;

    struct
    {
        uint8_t num_tracks;
        uint8_t step_rate;
        uint8_t sectors_per_trackM;
        uint8_t sectors_per_trackL;
        uint8_t num_sides;
        uint8_t density;
        uint8_t sector_sizeM;
        uint8_t sector_sizeL;
        uint8_t drive_present;
        uint8_t reserved1;
        uint8_t reserved2;
        uint8_t reserved3;
    } _percomBlock;

    void sio_read();
    void sio_write(bool verify);
    void sio_format();
    void sio_status() override;
    void sio_process() override;

    void derive_percom_block(uint16_t numSectors);
    void sio_read_percom_block();
    void sio_write_percom_block();
    void dump_percom_block();

public:
    disktype_t mount(FILE *f, const char *filename, disktype_t disk_type = DISKTYPE_UNKNOWN);
    void umount();
    bool write_blank_atr(FILE *f, uint16_t sectorSize, uint16_t numSectors);

    FILE *file() { return _file; };
    disktype_t disktype() { return _disktype; };
};

#endif

