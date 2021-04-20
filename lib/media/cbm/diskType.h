#ifndef _DISKTYPE_
#define _DISKTYPE_

#include <stdio.h>

#define INVALID_SECTOR_VALUE 65536

#define DISK_SECTORBUF_SIZE 256

#define DISK_BYTES_PER_SECTOR_SINGLE 128
#define DISK_BYTES_PER_SECTOR_DOUBLE 256

#define DISK_CTRL_STATUS_CLEAR 0x00
#define DISK_CTRL_STATUS_BUSY 0x01
#define DISK_CTRL_STATUS_DATA_PENDING 0x02
#define DISK_CTRL_STATUS_DATA_LOST 0x04
#define DISK_CTRL_STATUS_CRC_ERROR 0x08
#define DISK_CTRL_STATUS_SECTOR_MISSING 0x10
#define DISK_CTRL_STATUS_SECTOR_DELETED 0x20
#define DISK_CTRL_STATUS_WRITE_PROTECT_ERROR 0x40

#define DISK_DRIVE_STATUS_CLEAR 0x00
#define DISK_DRIVE_STATUS_RCV_ERR_CMD_FRAME 0x01
#define DISK_DRIVE_STATUS_RCV_ERR_DAT_FRAME 0x02
#define DISK_DRIVE_STATUS_PUT_FAILED 0x04
#define DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR 0x08
#define DISK_DRIVE_STATUS_MOTOR_RUNNING 0x10
#define DISK_DRIVE_STATUS_DOUBLE_DENSITY 0x20
#define DISK_DRIVE_STATUS_DOUBLE_SIDED 0x40
#define DISK_DRIVE_STATUS_ENHANCED_DENSITY 0x80

enum disktype_t 
{
    DISKTYPE_UNKNOWN = 0,
    DISKTYPE_ATR,
    DISKTYPE_ATX,
    DISKTYPE_XEX,
    DISKTYPE_CAS,
    DISKTYPE_WAV,
    DISKTYPE_COUNT
};

class DiskType
{
protected:
    FILE *_disk_fileh = nullptr;
    uint32_t _disk_image_size = 0;
    uint32_t _disk_num_sectors = 0;
    uint16_t _disk_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;
    int32_t _disk_last_sector = INVALID_SECTOR_VALUE;
    uint8_t _disk_controller_status = DISK_CTRL_STATUS_CLEAR;

public:
    struct
    {
        uint8_t num_tracks;
        uint8_t step_rate;
        uint8_t sectors_per_trackH;
        uint8_t sectors_per_trackL;
        uint8_t num_sides;
        uint8_t density;
        uint8_t sector_sizeH;
        uint8_t sector_sizeL;
        uint8_t drive_present;
        uint8_t reserved1;
        uint8_t reserved2;
        uint8_t reserved3;
    } _percomBlock;

    uint8_t _disk_sectorbuff[DISK_SECTORBUF_SIZE];

    disktype_t _disktype = DISKTYPE_UNKNOWN;
    bool _allow_hsio = true;

    virtual disktype_t mount(FILE *f, uint32_t disksize) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *respopnsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint16_t sectornum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint16_t sectornum, bool verify);

    // Always returns 128 for the first 3 sectors, otherwise _sectorSize
    virtual uint16_t sector_size(uint16_t sectornum);
    
    virtual void status(uint8_t statusbuff[4]) = 0;

    static disktype_t discover_disktype(const char *filename);

    void dump_percom_block();
    void derive_percom_block(uint16_t numSectors);

    virtual ~DiskType();
};

#endif // _DISKTYPE_
