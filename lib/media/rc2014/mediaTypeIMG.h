#ifndef _MEDIATYPE_IMG_
#define _MEDIATYPE_IMG_

#include <string>

#include <utility>

#include "mediaType.h"

/**
 * This describes an 8MB "slice" used in RC2014 CF modules
 * 
 * From Grant Searle (http://searle.x10host.com/cpm/index.html)
 * 
 * Each sector is 512 bytes and contains 4 x CP/M sectors (128 bytes each).
 * The CP/M track size is arbitrary, so 128 sectors per track seems a suitable
 * value to use - this makes 32 "blocked" sectors per track.
 * To get 8MB disks, this therefore requires 512 tracks, so 9 bits are used
 * for the track number.
*/




class MediaTypeIMG : public MediaType
{
private:
    uint32_t _sector_to_offset(uint16_t sectorNum);

public:
    struct CpmDiskImageDetails {
        std::string file_extension;
        uint32_t media_size;
        CPM_DPB dpb;
    };

public:
    virtual bool read(uint16_t sectornum, uint16_t *readcount) override;
    virtual bool write(uint16_t sectornum, bool verify) override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize, mediatype_t disk_type) override;

    virtual void status(uint8_t statusbuff[4]) override;

    static bool create(FILE *f, uint16_t sectorSize, uint16_t numSectors);
};


#endif // _MEDIATYPE_DSK_