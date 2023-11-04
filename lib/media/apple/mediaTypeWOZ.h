#ifndef _MEDIATYPE_WOZ_
#define _MEDIATYPE_WOZ_

#include <stdio.h>

#include "mediaType.h"

#define MAX_TRACKS 160
#define WOZ1_TRACK_LEN 6646
#define WOZ1_NUM_BLKS 13
#define WOZ1_BIT_TIME 32
struct TRK_t
{
    uint16_t start_block;
    uint16_t block_count;
    uint32_t bit_count;
};


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
    TRK_t trks[MAX_TRACKS];
    uint8_t *trk_ptrs[MAX_TRACKS] = { };

public:
    virtual bool read(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override { return false; };
    virtual bool write(uint32_t blockNum, uint16_t *count, uint8_t* buffer) override { return false; };

    virtual bool format(uint16_t *respopnsesize) override { return false; };

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    virtual void unmount() override;

    virtual bool status() override {return (_media_fileh != nullptr);}

    uint8_t trackmap(uint8_t t) { return tmap[t]; };
    uint8_t *get_track(int t) { return trk_ptrs[tmap[t]]; };
    int track_len(int t) { return trks[tmap[t]].block_count * 512; };
    int num_bits(int t) { return trks[tmap[t]].bit_count; };
    uint8_t optimal_bit_timing;
    // static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_WOZ_
