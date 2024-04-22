#ifndef _MEDIA_TYPE_
#define _MEDIA_TYPE_

#include <string>

#define INVALID_SECTOR_VALUE 0xFFFFFFFF

#define DISK_BYTES_PER_SECTOR_SINGLE 512
#define DISK_BYTES_PER_SECTOR_BLOCK 512

#define DISK_CTRL_STATUS_CLEAR 0x00

enum mediatype_t 
{
    MEDIATYPE_UNKNOWN = 0,
    MEDIATYPE_IMG_HD,      // 8meg RomWBW disk splice  
    MEDIATYPE_IMG_FD720,   // 3.5" DS/DD Floppy Drive (720K)
    MEDIATYPE_IMG_FD720_PCW256,   // 3.5" DS/DD Floppy Drive (720K) - SAM Coupe Pro-DOS format
    MEDIATYPE_IMG_FD144,   // 3.5" DS/HD Floppy Drive (1.44M)
    MEDIATYPE_IMG_FD360,   // 5.25" DS/DD Floppy Drive (360K) 
    MEDIATYPE_IMG_FD120,   // 5.25" DS/HD Floppy Drive (1.2M)
    MEDIATYPE_IMG_FD111,   // 8" DS/DD Floppy Drive (1.11M)
    //MEDIATYPE_IMG_IBM3740, // 8" SS/SD Floppy Drive (237.5KB)

    MEDIATYPE_COUNT
};

class MediaType
{
protected:
    FILE *_media_fileh = nullptr;
    uint32_t _media_image_size = 0;
    uint32_t _media_num_sectors = 0;
    uint16_t _media_sector_size = DISK_BYTES_PER_SECTOR_SINGLE;

public:
    struct CPM_DPB {
        uint16_t spt;  // ;Number of 128-byte records per track
        uint8_t bsh;   // ;Block shift. 3 => 1k, 4 => 2k, 5 => 4k....
        uint8_t blm;   // ;Block mask. 7 => 1k, 0Fh => 2k, 1Fh => 4k...
        uint8_t exm;   // ;Extent mask, see later
        uint16_t dsm;  // ;(no. of blocks on the disc)-1
        uint16_t drm;  // ;(no. of directory entries)-1
        uint8_t al0;   // ;Directory allocation bitmap, first byte
        uint8_t al1;   // ;Directory allocation bitmap, second byte
        uint16_t cks;  // ;Checksum vector size, 0 or 8000h for a fixed disc.
        uint16_t off;  // ;Offset, number of reserved tracks

        uint8_t psh;   // ;Physical sector shift, 0 => 128-byte sectors
                       // ;1 => 256-byte sectors  2 => 512-byte sectors...
        uint8_t phm;   // ;Physical sector mask,  0 => 128-byte sectors
                       // ;1 => 256-byte sectors, 3 => 512-byte sectors...
    };

    struct DiskImageDetails {
        mediatype_t media_type;
        std::string media_type_string;
        std::string file_extension;
        uint32_t media_size;
    };

    uint8_t _media_sectorbuff[DISK_BYTES_PER_SECTOR_SINGLE];
    uint32_t _media_last_sector = INVALID_SECTOR_VALUE-1;
    uint8_t _media_controller_status = DISK_CTRL_STATUS_CLEAR;

    mediatype_t _mediatype = MEDIATYPE_UNKNOWN;

    virtual mediatype_t mount(FILE *f, uint32_t disksize, mediatype_t disk_type) = 0;
    virtual void unmount();

    // Returns TRUE if an error condition occurred
    virtual bool format(uint16_t *responsesize);

    // Returns TRUE if an error condition occurred
    virtual bool read(uint16_t sectornum, uint16_t *readcount) = 0;
    // Returns TRUE if an error condition occurred
    virtual bool write(uint16_t sectornum, bool verify);
    
    virtual void status(uint8_t statusbuff[4]) = 0;

    static mediatype_t discover_mediatype(const char *filename, uint32_t disksize);
    uint16_t sector_size(uint16_t sector);

    virtual ~MediaType();
};

#endif // _MEDIA_TYPE_
