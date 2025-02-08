#ifdef BUILD_ADAM

#include "mediaTypeROM.h"

#include <cstring>

#include "../../include/debug.h"
#include "fnSystem.h"

#define ROM_BLOCK_SIZE 512

MediaTypeROM::MediaTypeROM()
{
    rom = (char *)malloc(32768);
}

MediaTypeROM::~MediaTypeROM()
{
    free(rom);
}

// Returns byte offset of given sector number
uint32_t MediaTypeROM::_block_to_offset(uint32_t blockNum)
{
    return blockNum * 1024;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::read(uint32_t blockNum, uint16_t *readcount)
{
    bool err = false;
    
    if (blockNum == _media_last_block)
        return false; // We already have block.

    Debug_print("ROM READ\r\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks - 1)
    {
        Debug_printf("::read block %lu > %lu\r\n", blockNum, _media_num_blocks);
        _media_controller_status = 2;
        return true;
    }

    memset(_media_blockbuff, 0, sizeof(_media_blockbuff));

    if (blockNum == 0)
    {
        memcpy(_media_blockbuff, block0, sizeof(_media_blockbuff));
    }
    else if (blockNum == 1)
    {
        memcpy(_media_blockbuff, block1, sizeof(_media_blockbuff));
    }
    else // (blocknum > 1)
    {
        // // Perform a seek if we're not reading the sector after the last one we read
        uint32_t offset = _block_to_offset(blockNum - 2); // minus the two boot blocks
        err = fseek(_media_fileh, offset, SEEK_SET) != 0;
        _media_last_block = INVALID_SECTOR_VALUE;

        if (err == false)
            err = fread(_media_blockbuff, 1, 1024, _media_fileh) != 1024;

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
    }
    return err;

    // The old code is here.
    // if (blockNum == 0)
    //     memcpy(_media_blockbuff, block0, sizeof(_media_blockbuff));
    // else if (blockNum == 1)
    //     memcpy(_media_blockbuff, block1, sizeof(_media_blockbuff));
    // else
    // {
    //     blockNum -= 2;
    //     if (blockNum < 32)
    //         memcpy(_media_blockbuff, &rom[blockNum * 1024], sizeof(_media_blockbuff));
    //     else
    //     {
    //         _media_controller_status = 2;
    //         return true;
    //     }
    // }
    // return false;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::write(uint32_t blockNum, bool verify)
{
    return true;
}

uint8_t MediaTypeROM::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
bool MediaTypeROM::format(uint16_t *responsesize)
{
    return true;
}

mediatype_t MediaTypeROM::mount(FILE *f, uint32_t disksize)
{
    Debug_print("ROM MOUNT\r\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_ROM;
    _media_num_blocks = disksize / 1024;
    _media_num_blocks += 2; // to account for the two boot blocks.

    Debug_printv("FLAGS: %x\n", _media_fileh->_flags);
    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeROM::create(FILE *f, uint32_t numBlocks)
{
    return true;
}
#endif /* BUILD_ADAM */