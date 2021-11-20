#ifdef BUILD_ADAM
#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "../device/adamnet/disk.h"

#include "mediaTypeROM.h"

// Returns TRUE if an error condition occurred
bool MediaTypeROM::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == 0)
        memcpy(_media_blockbuff, block0, sizeof(_media_blockbuff));
    else if (blockNum == 1)
        memcpy(_media_blockbuff, block1, sizeof(_media_blockbuff));
    else
    {
        blockNum -= 2;
        if (blockNum < 32)
            memcpy(_media_blockbuff, &rom[blockNum * 1024], sizeof(_media_blockbuff));
        else
            // Error
            return true;
    }
    return false;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::write(uint16_t blockNum, bool verify)
{
    return true;
}

void MediaTypeROM::status(uint8_t statusbuff[4])
{
    // Currently not being used.
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::format(uint16_t *responsesize)
{
    return true;
}

mediatype_t MediaTypeROM::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ROM MOUNT\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_ROM;

    if (disksize > 32768)
        disksize = 32768;

    // Load ROM into memory.
    if (fread(rom, 1, disksize, f) != disksize)
    {
        _media_fileh = nullptr;
        return MEDIATYPE_UNKNOWN;
    }

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeROM::create(FILE *f, uint32_t numBlocks)
{
    return true;
}
#endif /* BUILD_ADAM */