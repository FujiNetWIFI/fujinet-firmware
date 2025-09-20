#ifndef _MEDIA_TYPE_
#define _MEDIA_TYPE_

#include <stdio.h>

#define INVALID_SECTOR_VALUE 0xFFFFFFFF

#define MEDIA_BLOCK_SIZE 256

#define DISK_CTRL_STATUS_CLEAR 0x00

enum mediatype_t 
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_DDP,
    MEDIATYPE_DSK,
    MEDIATYPE_ROM,
    MEDIATYPE_COUNT
};

class MediaType
{
protected:
    FILE *_media_fileh = nullptr;
    uint32_t _media_image_size = 0;
    uint32_t _media_num_blocks = 256;
    //uint16_t _media_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;

public:
    uint8_t _media_blockbuff[MEDIA_BLOCK_SIZE];
    uint32_t _media_last_block = INVALID_SECTOR_VALUE-1;
    uint8_t _media_controller_status = DISK_CTRL_STATUS_CLEAR;

    mediatype_t _mediatype = MEDIATYPE_UNKNOWN;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *responsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint32_t blockNum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint32_t blockNum, bool verify);
    
    virtual uint8_t status() = 0;

    static mediatype_t discover_mediatype(const char *filename);

    uint32_t num_blocks() { return _media_num_blocks; }

    virtual ~MediaType();
};

#endif // _MEDIA_TYPE_
