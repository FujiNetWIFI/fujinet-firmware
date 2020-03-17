#ifndef DISK_H
#define DISK_H
#include <Arduino.h>

#include "sio.h"
#include <FS.h>

extern int command_frame_counter;
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
    File *_file;

    unsigned short sectorSize = 128;
    byte sector[256];

    byte sectorCache[2560];
    bool cacheError[9];
    int firstCachedSector = 65535;
    unsigned char max_cached_sectors = 19;

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
    void sio_write();
    void sio_format();
    void sio_status() override;
    void sio_process() override;

    void derive_percom_block(unsigned short numSectors);
    void sio_read_percom_block();
    void sio_write_percom_block();
    void dump_percom_block();

    void sio_high_speed();

public:
    void mount(File *f);
    void umount();
    void invalidate_cache();
    bool write_blank_atr(File *f, unsigned short sectorSize, unsigned short numSectors);
    File *file();
};

#endif // guard