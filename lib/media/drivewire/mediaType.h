#ifndef _MEDIA_TYPE_
#define _MEDIA_TYPE_

#include <stdio.h>
#include <fujiHost.h>

#define INVALID_SECTOR_VALUE 0xFFFFFFFF

#define MEDIA_BLOCK_SIZE 256

#define DISK_CTRL_STATUS_CLEAR 0x00

enum mediatype_t 
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_DSK,
    MEDIATYPE_MRM,
    MEDIATYPE_VDK,    
    MEDIATYPE_COUNT
};

class MediaType
{
protected:
    fnFile *_media_fileh = nullptr;
    uint32_t _media_image_size = 0;
    uint32_t _media_num_blocks = 256;
    uint16_t _media_sector_size = MEDIA_BLOCK_SIZE;

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

    uint8_t _media_blockbuff[MEDIA_BLOCK_SIZE];
    uint32_t _media_last_block = INVALID_SECTOR_VALUE-1;
    uint8_t _media_controller_status = DISK_CTRL_STATUS_CLEAR;
    fujiHost *_media_host = nullptr;
    char _disk_filename[256];


    mediatype_t _mediatype = MEDIATYPE_UNKNOWN;
    bool _allow_hsio = true;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *responsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint32_t blockNum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint32_t blockNum, bool verify);

    virtual void get_block_buffer(uint8_t **p_buffer, uint16_t *p_blk_size);
    
    virtual uint8_t status() = 0;

    static mediatype_t discover_mediatype(const char *filename);

    void dump_percom_block();
    void derive_percom_block(uint16_t numSectors);

    virtual ~MediaType();
};

#endif // _MEDIA_TYPE_
