#ifndef _MEDIA_TYPE_
#define _MEDIA_TYPE_

#include <stdint.h>
#include "fnio.h"
#include"../fuji/fujiHost.h"

#define INVALID_SECTOR_VALUE 65536

#define DISK_SECTORBUF_SIZE 512

#define DISK_BYTES_PER_SECTOR_SINGLE 128
#define DISK_BYTES_PER_SECTOR_DOUBLE 256
#define DISK_BYTES_PER_SECTOR_DOUBLE_DOUBLE 512

#define DISK_CTRL_STATUS_CLEAR 0x00

enum mediatype_t
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_DO,
    MEDIATYPE_DSK,
    MEDIATYPE_PO,
    MEDIATYPE_WOZ,
    MEDIATYPE_COUNT
};

class MediaType
{
protected:
    fnFile *_media_fileh = nullptr;
    fnFile *oldFileh = nullptr; /* Temp fileh for high score enabled games */
    fnFile *hsFileh = nullptr; /* Temp fileh for high score enabled games */

    uint32_t _media_image_size = 0;
    uint32_t _media_num_sectors = 0;
    uint16_t _media_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;
    int32_t _media_last_sector = INVALID_SECTOR_VALUE;
    uint8_t _media_controller_status = DISK_CTRL_STATUS_CLEAR;
    uint16_t _high_score_block_lb = 0; /* High score block (lower bound) to allow write. 1-65535 */
    uint16_t _high_score_block_ub = 0; /* High score block (upper bound) to allow write. 1-65535 */

public:
    // struct
    // {
    //     uint8_t num_tracks;
    //     uint8_t step_rate;
    //     uint8_t sectors_per_trackH;
    //     uint8_t sectors_per_trackL;
    //     uint8_t num_sides;
    //     uint8_t density;
    //     uint8_t sector_sizeH;
    //     uint8_t sector_sizeL;
    //     uint8_t drive_present;
    //     uint8_t reserved1;
    //     uint8_t reserved2;
    //     uint8_t reserved3;
    // } _percomBlock;

    uint32_t num_blocks;
    // FILE* fileptr() {return _media_fileh;}

    char _disk_filename[256];
    fujiHost *_media_host = nullptr;
    fnFile *_media_hsfileh = nullptr;
    bool high_score_enabled = false;

    // uint8_t _media_sectorbuff[DISK_SECTORBUF_SIZE];

    mediatype_t _mediatype = MEDIATYPE_UNKNOWN;
    // bool _allow_hsio = true;
    bool diskiiemulation;

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *responsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) = 0;
    virtual bool write_sector(int track, int sector, uint8_t *buffer) = 0;

    // virtual uint16_t sector_size(uint16_t sectornum);
    
    virtual bool status() = 0;

    static mediatype_t discover_mediatype(const char *filename);
    static mediatype_t discover_dsk_mediatype(fnFile* f, uint32_t disksize);

    // void dump_percom_block();
    // void derive_percom_block(uint16_t numSectors);

    virtual ~MediaType();
};

#endif // _MEDIA_TYPE_
