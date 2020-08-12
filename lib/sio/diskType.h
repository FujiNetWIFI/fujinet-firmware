#ifndef _DISKTYPE_
#define _DISKTYPE_

#include <stdio.h>

#define INVALID_SECTOR_VALUE 65536

#define DISK_SECTORBUF_SIZE 256

enum disktype_t {
    DISKTYPE_UNKNOWN = 0,
    DISKTYPE_ATR,
    DISKTYPE_ATX,
    DISKTYPE_XEX,
    DISKTYPE_COUNT
};

class DiskType
{
protected:
    FILE *_file = nullptr;
    uint32_t _imageSize = 0;
    
    uint32_t _numSectors = 0;

    uint16_t _sectorSize = 128;

    int32_t _lastSectorUsed = INVALID_SECTOR_VALUE;


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

    void dump_percom_block();
    void derive_percom_block(uint16_t numSectors);

    uint8_t _sectorbuff[DISK_SECTORBUF_SIZE];

    disktype_t _disktype = DISKTYPE_UNKNOWN;

    virtual disktype_t mount(FILE *f, uint32_t disksize) = 0;
    virtual void unmount();

    virtual ~DiskType();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *respopnsesize) = 0;

    // Returns TRUE if an error condition occurred
    virtual bool read(uint16_t sectornum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint16_t sectornum, bool verify) = 0;

    virtual uint16_t sector_size(uint16_t sectornum) = 0;
    
    virtual void status(uint8_t statusbuff[4]) = 0;

    static disktype_t discover_disktype(const char *filename);
};

#endif // _DISKTYPE_
