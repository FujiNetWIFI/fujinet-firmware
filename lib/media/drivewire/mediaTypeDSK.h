#ifndef _MEDIATYPE_DSK_
#define _MEDIATYPE_DSK_

#include <stdio.h>

#include "mediaType.h"

#define HS_MARKER_BLOCK 323     // track 17, sector 18: unused by Disk BASIC
#define HS_MAX_RANGES 4

class MediaTypeDSK : public MediaType
{
private:
    uint32_t _block_to_offset(uint32_t blockNum);
    void _parse_high_score_marker();
    bool _high_score_block(uint32_t blockNum);
    uint8_t _hs_num_ranges = 0;
    uint16_t _hs_start[HS_MAX_RANGES];
    uint16_t _hs_count[HS_MAX_RANGES];

public:
    error_is_true read(uint32_t blockNum, uint16_t *readcount) override;
    error_is_true write(uint32_t blockNum, bool verify) override;

    error_is_true format(uint16_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    uint8_t status() override;

    static success_is_true create(FILE *f, uint32_t numBlock);
};


#endif // _MEDIATYPE_DSK_
