#ifndef DISK_H
#define DISK_H

#include "sio.h"

extern int command_frame_counter;

// Specify sectors 0-UNCACHED_REGION to not be cached
// Normally, this is set to 3 to not cache the first three sectors, which
// may be only 128 bytes compared to 256 bytes for double density images.
// If you set this to 65535, then the cache is completely disabled.
// Please do not change this value yet, the cache has been ripped out
// due to be rewritten.
#define UNCACHED_REGION 65535

#define ATARISIO_ATARI_FREQUENCY_PAL 1773447
#define COMMAND_FRAME_SPEED_CHANGE_THRESHOLD 2
#define HISPEED_INDEX 0x00
// 0x06
// 10
// 0x00
#define HISPEED_BAUDRATE (ATARISIO_ATARI_FREQUENCY_PAL * 10) / (10 * (2 * (HISPEED_INDEX + 7)) + 3)
// 68837 
// 52640 
//125984
#define STANDARD_BAUDRATE 19200
#define SERIAL_TIMEOUT 300

unsigned short para_to_num_sectors(unsigned short para, unsigned char para_hi, unsigned short ss);
unsigned long num_sectors_to_para(unsigned short num_sectors, unsigned short sector_size);

class sioDisk : public sioDevice
{
private:
    FILE *_file;

    unsigned short sectorSize = 128;
    byte sector[256];

    byte sectorCache[4][256];
    unsigned short lastSectorNum = 65535;

    struct
    {
        unsigned char num_tracks;
        unsigned char step_rate;
        unsigned char sectors_per_trackM;
        unsigned char sectors_per_trackL;
        unsigned char num_sides;
        unsigned char density;
        unsigned char sector_sizeM;
        unsigned char sector_sizeL;
        unsigned char drive_present;
        unsigned char reserved1;
        unsigned char reserved2;
        unsigned char reserved3;
    } percomBlock;

    void sio_read();
    void sio_write(bool verify);
    void sio_format();
    void sio_status() override;
    void sio_process() override;

    void derive_percom_block(unsigned short numSectors);
    void sio_read_percom_block();
    void sio_write_percom_block();
    void dump_percom_block();

    void sio_high_speed();

public:
    void mount(FILE *f);
    void umount();
    void invalidate_cache();
    bool write_blank_atr(FILE *f, unsigned short sectorSize, unsigned short numSectors);
    FILE *file();
};

    long sector_offset(unsigned short sectorNum, unsigned short sectorSize);
    unsigned short sector_size(unsigned short sectorNum, unsigned short sectorSize);

#endif // guard