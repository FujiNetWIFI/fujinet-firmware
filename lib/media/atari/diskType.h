#ifndef _MEDIATYPE_
#define _MEDIATYPE_

#include <stdint.h>
#include "fnio.h"
#include "fujiHost.h"

#define INVALID_SECTOR_VALUE 65536

#define DISK_SECTORBUF_SIZE 512

#define DISK_BYTES_PER_SECTOR_SINGLE 128
#define DISK_BYTES_PER_SECTOR_DOUBLE 256
#define DISK_BYTES_PER_SECTOR_DOUBLE_DOUBLE 512

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

enum mediatype_t 
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_ATR,
    MEDIATYPE_ATX,
    MEDIATYPE_XEX,
    MEDIATYPE_CAS,
    MEDIATYPE_WAV,
    MEDIATYPE_COUNT
};

class MediaType
{
protected:
    fnFile *_disk_fileh = nullptr;
    uint32_t _disk_image_size = 0;
    int32_t _disk_last_sector = INVALID_SECTOR_VALUE;
    uint8_t _disk_controller_status = DISK_CTRL_STATUS_CLEAR;
    bool _disk_readonly = true;
    uint16_t _high_score_sector = 0; /* High score sector to allow write. 1-65535 */
    uint8_t _high_score_num_sectors = 0;
    
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

    char _disk_filename[256];

    uint8_t _disk_sectorbuff[DISK_SECTORBUF_SIZE];
    uint32_t _disk_num_sectors = 0;
    uint16_t _disk_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;

    fujiHost *_disk_host = nullptr;

    mediatype_t _disktype = MEDIATYPE_UNKNOWN;
    bool _allow_hsio = true;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *responsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint16_t sectornum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint16_t sectornum, bool verify);

    // Always returns 128 for the first 3 sectors, otherwise _sectorSize
    virtual uint16_t sector_size(uint16_t sectornum);
    
    virtual void status(uint8_t statusbuff[4]) = 0;

    static mediatype_t discover_mediatype(const char *filename);

    void dump_percom_block();
    void derive_percom_block(uint16_t numSectors);

    virtual ~MediaType();
};

#endif // _MEDIATYPE_
