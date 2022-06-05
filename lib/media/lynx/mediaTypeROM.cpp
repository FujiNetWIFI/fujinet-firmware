#ifdef BUILD_LYNX

#include "mediaTypeROM.h"

#include <cstdint>
#include <cstring>

#include "../../include/debug.h"


// Returns byte offset of given sector number
uint32_t MediaTypeROM::_block_to_offset(uint32_t blockNum)
{
    return blockNum * 256;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        return false; // We already have block.

    Debug_print("LNX READ\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks-1)
    {
        Debug_printf("::read block %d > %d\n", blockNum, _media_num_blocks);
        _media_controller_status=2;
        return true;
    }

    memset(_media_blockbuff, 0, sizeof(_media_blockbuff));

    bool err = false;
    // // Perform a seek if we're not reading the sector after the last one we read
     if (blockNum != _media_last_block + 1)
     {
        uint32_t offset = _block_to_offset(blockNum);
        err = fseek(_media_fileh, offset, SEEK_SET) != 0;
        _media_last_block = INVALID_SECTOR_VALUE;
     }

    if (err == false)
        err = fread(_media_blockbuff, 1, 256, _media_fileh) != 256;

    if (err == false)
    {
        _media_last_block = blockNum;
        _media_controller_status = 0;
        return false;
    }
    else
    {
        _media_last_block = INVALID_SECTOR_VALUE;
        _media_controller_status = 2;
        return true;
    }

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::write(uint32_t blockNum, bool verify)
{
    return false;
}

uint8_t MediaTypeROM::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::format(uint16_t *responsesize)
{
    return false;
}

mediatype_t MediaTypeROM::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ROM MOUNT\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_ROM;
    _media_num_blocks = disksize / 256;

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeROM::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("ROM CREATE\n");

    return true;
}


#endif /* BUILD_LYNX */