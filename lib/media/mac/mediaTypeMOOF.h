#ifndef _MEDIATYPE_MOOF_
#define _MEDIATYPE_MOOF_
//  https://applesaucefdc.com/moof-reference/

#include "mediaType.h"
#include <stdio.h>

#define MAX_CYLINDERS 80
#define MAX_SIDES 2
#define MAX_TRACKS (MAX_SIDES * MAX_CYLINDERS)

#define CACHE_IMAGE

struct TRK_t
{
    uint16_t start_block;
    uint16_t block_count;
    uint32_t bit_count;
};

enum class moof_disk_type_t
{
    UNKNOWN,
    SSDD_GCR,
    DSDD_GCR,
    DSDD_MFM
};

class MediaTypeMOOF : public MediaType
{
private:
    moof_disk_type_t moof_disktype;

    bool moof_check_header();
    bool moof_read_info();
    bool moof_read_tmap();
    bool moof_read_tracks();

protected:
    uint8_t tmap[MAX_TRACKS];
    TRK_t trks[MAX_TRACKS];
#ifdef CACHE_IMAGE
    uint8_t *trk_ptrs[MAX_TRACKS] = {};
#else
    uint8_t *trk_buffer;
#endif

public:
    MediaTypeMOOF() {};
    
    virtual bool read(uint32_t blockNum, uint8_t *buffer) override { return true; };
    virtual bool write(uint32_t blockNum, uint8_t *buffer) override { return true; };

    virtual bool format(uint16_t *responsesize) override { return false; };

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    mediatype_t mount(FILE *f) { return mount(f, 0); };
    virtual void unmount() override;

    virtual bool status() override { return (_media_fileh != nullptr); }

    uint8_t trackmap(uint8_t t) { return tmap[t]; };
    uint8_t *get_track(int t);
    int track_len(int t) { return trks[tmap[t]].block_count * 512; };
    int num_bits(int t) { return trks[tmap[t]].bit_count; };
    uint8_t optimal_bit_timing;
    // static bool create(FILE *f, uint32_t numBlock);
};

#endif // guard
