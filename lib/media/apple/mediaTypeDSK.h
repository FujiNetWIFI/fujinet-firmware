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


class MediaTypeDSK  : public MediaTypeWOZ
{
private:
    size_t num_tracks = 0;

    void dsk2woz_info();
    void dsk2woz_tmap();
    bool dsk2woz_tracks(uint8_t *dsk); 

public:

#ifdef ESP_PLATFORM
    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
#else
    virtual mediatype_t mount(FileHandler *f, uint32_t disksize) override;
#endif
    // virtual void unmount() override;

    // static bool create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_DSK_
