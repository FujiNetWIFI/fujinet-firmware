#ifndef _MEDIATYPE_DSK_
#define _MEDIATYPE_DSK_

#include <stdio.h>

#include "mediaTypeWOZ.h"

// #define MAX_TRACKS 160

// struct TRK_t
// {
//     uint16_t start_block;
//     uint16_t block_count;
//     uint32_t bit_count;
// };

extern uint16_t decode_6_and_2(uint8_t *dest, const uint8_t *src);

class MediaTypeDSK  : public MediaTypeWOZ
{
private:
    size_t num_tracks = 0;

    void dsk2woz_info();
    void dsk2woz_tmap();
    bool dsk2woz_tracks(uint8_t *dsk); 

public:

    virtual mediatype_t mount(fnFile *f, uint32_t disksize) override;
    // virtual void unmount() override;
    virtual bool write_sector(int track, int sector, uint8_t *buffer) override;

    // static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_DSK_
