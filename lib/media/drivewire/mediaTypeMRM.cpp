#ifdef BUILD_COCO

#include "mediaTypeMRM.h"

#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <errno.h>

#include "../../include/debug.h"

// Returns byte offset of given sector number
uint32_t MediaTypeMRM::_block_to_offset(uint32_t blockNum)
{
    return blockNum * MRM_BLOCK_SIZE;
}

// Returns TRUE if an error condition occurred
bool MediaTypeMRM::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        return false; // We already have block.

    // Debug_print("MRM READ\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum >= _media_num_blocks)
    {
        Debug_printf("::read block %lu >= %lu\n", blockNum, _media_num_blocks);
        _media_controller_status = 2;
        return true;
    }

    memset(_media_blockbuff, 0xFF, sizeof(_media_blockbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (blockNum != _media_last_block + 1)
    {
        uint32_t offset = _block_to_offset(blockNum);
        err = fnio::fseek(_media_fileh, offset, SEEK_SET) != 0;
    }
    
    uint16_t to_read;
    if (_block_to_offset(blockNum + 1) > _media_image_size)
        to_read = _media_image_size - _block_to_offset(blockNum);
    else
        to_read = MRM_BLOCK_SIZE;

    if (err == false)
        err = fnio::fread(_media_blockbuff, 1, to_read, _media_fileh) != to_read;

    if (err == false)
        _media_last_block = blockNum;
    else 
        _media_last_block = INVALID_SECTOR_VALUE;

    _media_controller_status = 0;

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeMRM::write(uint32_t blockNum, bool verify)
{
    Debug_printf("MRM WRITE - not implemented\n");

    return true;
}

void MediaTypeMRM::get_block_buffer(uint8_t **p_buffer, uint16_t *p_blk_size)
{
    *p_buffer = &_media_blockbuff[0];
    *p_blk_size = MRM_BLOCK_SIZE;
}

uint8_t MediaTypeMRM::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
bool MediaTypeMRM::format(uint16_t *responsesize)
{
    Debug_printf("MRM FORMAT - not implemented\n");

    return true;
}

mediatype_t MediaTypeMRM::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("DSK MOUNT\n");

    _media_fileh = f;
    _media_image_size = disksize;
    _mediatype = MEDIATYPE_MRM;
    _media_num_blocks = (disksize + MRM_BLOCK_SIZE - 1) / MRM_BLOCK_SIZE;

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeMRM::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("MRM CREATE - not implemented\n");

    return false;
}
#endif // BUILD_COCO
