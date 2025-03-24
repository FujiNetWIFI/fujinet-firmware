#ifndef _MEDIATYPE_WOZ_
#define _MEDIATYPE_WOZ_

#include <stdio.h>

#include "mediaType.h"

#define MAX_TRACKS 160
#define WOZ1_TRACK_LEN 6646
#define WOZ1_NUM_BLKS 13
#define WOZ1_BIT_TIME 32
struct TRK_bitstream
{
    uint16_t len_blocks;
    uint16_t len_bytes;
    uint32_t len_bits;
    uint8_t data[];
};

#define BITSTREAM_ALLOC_SIZE(x) (sizeof(TRK_bitstream) + x)

class MediaTypeWOZ : public MediaType
{
private:
    char woz_version;

    bool wozX_check_header();
    bool wozX_read_info();
    bool wozX_read_tmap();
    bool woz1_read_tracks();
    bool woz2_read_tracks();

protected:
    uint8_t tmap[MAX_TRACKS];
    TRK_bitstream *trk_data[MAX_TRACKS];

public:
    virtual bool read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override { return false; };
    virtual bool write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override { return false; };
    virtual bool write_sector(int track, int sector, uint8_t *buffer) override;

    virtual bool format(uint16_t *responsesize) override { return false; };

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;
    virtual void unmount() override;

    virtual bool status() override {return (_media_fileh != nullptr);}

    uint8_t trackmap(uint8_t t) { return tmap[t]; };
    TRK_bitstream *get_track(int t) { return trk_data[tmap[t]]; };
    uint8_t optimal_bit_timing;
    // static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_WOZ_
