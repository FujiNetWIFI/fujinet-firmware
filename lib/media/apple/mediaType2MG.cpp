#ifdef BUILD_APPLE

#include "mediaType2MG.h"

#include <cstring>

#include "utils.h"
#include "../../include/debug.h"

mediatype_t MediaType2MG::mount(fnFile *f, uint32_t disksize)
{
    char hdr[64];

    diskiiemulation = false;
    fnio::fseek(f, 0, SEEK_SET);
    if (fnio::fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || memcmp(hdr, "2IMG", 4) != 0)
        return MEDIATYPE_UNKNOWN;

    // High score enabled: 'H','I' at offset 48 (0x30), then the writable block range,
    // lower and upper bound, little endian. An upper bound of 0 means just the lower.
    if (hdr[48] == 'H' && hdr[49] == 'I')
    {
        _high_score_block_lb = UINT16_FROM_HILOBYTES(hdr[51], hdr[50]);
        _high_score_block_ub = UINT16_FROM_HILOBYTES(hdr[53], hdr[52]);
        if (_high_score_block_lb > 0)
        {
            if (_high_score_block_ub == 0)
                _high_score_block_ub = _high_score_block_lb;

            high_score_enabled = true;
            Debug_printf("\r\nhigh score: block range: %04x..%04x\r\n", _high_score_block_lb, _high_score_block_ub);
        }
    }

    offset = sizeof(hdr);
    _media_fileh = f;
    num_blocks = (disksize - offset) / 512;
    return MEDIATYPE_2MG;
}

#endif // BUILD_APPLE
