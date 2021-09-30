#ifndef _MEDIA_TYPE_
#define _MEDIA_TYPE_

#include <stdio.h>


#define INVALID_SECTOR_VALUE 65536

#define DISK_SECTORBUF_SIZE 512

#define DISK_BYTES_PER_SECTOR_SINGLE 128
#define DISK_BYTES_PER_SECTOR_DOUBLE 256
#define DISK_BYTES_PER_SECTOR_DOUBLE_DOUBLE 512

#define DISK_CTRL_STATUS_CLEAR 0x00

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
    FILE *_media_fileh = nullptr;
    uint32_t _media_image_size = 0;
    uint32_t _media_num_sectors = 0;
    uint16_t _media_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;
    int32_t _media_last_sector = INVALID_SECTOR_VALUE;
    uint8_t _media_controller_status = DISK_CTRL_STATUS_CLEAR;

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

    uint8_t _media_sectorbuff[DISK_SECTORBUF_SIZE];

    mediatype_t _mediatype = MEDIATYPE_UNKNOWN;
    bool _allow_hsio = true;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) = 0;
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

    static mediatype_t discover_mediatype(const char *filename);

    void dump_percom_block();
    void derive_percom_block(uint16_t numSectors);

    virtual ~MediaType();
};

#endif // _MEDIA_TYPE_
