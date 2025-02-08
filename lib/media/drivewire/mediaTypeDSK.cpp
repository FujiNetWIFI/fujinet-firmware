#ifdef BUILD_COCO

#include "mediaTypeDSK.h"

#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <errno.h>

#include "../../include/debug.h"

// Returns byte offset of given sector number
uint32_t MediaTypeDSK::_block_to_offset(uint32_t blockNum)
{
    return blockNum * MEDIA_BLOCK_SIZE;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        return false; // We already have block.

    // Debug_print("DW DSK READ\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks)
    {
        Debug_printf("::read block %lu > %lu\n", blockNum, _media_num_blocks);
        _media_controller_status = 2;
        return true;
    }

    memset(_media_blockbuff, 0, sizeof(_media_blockbuff));

    bool err = false;
    // Perform a seek if we're not reading the sector after the last one we read
    if (blockNum != _media_last_block + 1)
    {
        uint32_t offset = _block_to_offset(blockNum);
        err = fnio::fseek(_media_fileh, offset, SEEK_SET) != 0;
    }
    
    if (err == false)
        err = fnio::fread(_media_blockbuff, 1, MEDIA_BLOCK_SIZE, _media_fileh) != MEDIA_BLOCK_SIZE;

    if (err == false)
        _media_last_block = blockNum;
    else
        _media_last_block = INVALID_SECTOR_VALUE;

    _media_controller_status = 0;

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::write(uint32_t blockNum, bool verify)
{
    // Debug_printf("DSK WRITE\n", blockNum, _media_num_blocks);

    uint32_t offset = _block_to_offset(blockNum);

    _media_last_block = INVALID_SECTOR_VALUE;

    // Perform a seek if we're writing to the sector after the last one
    int e;
    e = fnio::fseek(_media_fileh, offset, SEEK_SET);
    if (e != 0)
    {
        Debug_printf("::write seek error %d\n", e);
        _media_controller_status = 2;
        return true;
    }
    // Write the data
    e = fnio::fwrite(&_media_blockbuff, 1, MEDIA_BLOCK_SIZE, _media_fileh);

    if (e != MEDIA_BLOCK_SIZE)
    {
        Debug_printf("::write error %d, %d\n", e, errno);
        return true;
    }

    int ret = fnio::fflush(_media_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    
    // This next line is commented out because there's no fsync in the 
    // fnio class. In a discussion with @apc from the following discussion:
    // https://discord.com/channels/655893677146636301/1209535440915406848/1231880068528214026
    // fnio::fflush() should be sufficient for syncing as well.
//    ret = fsync(fileno(_media_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("DSK::write fsync:%d\n", ret);

    _media_last_block = INVALID_SECTOR_VALUE;
    _media_controller_status = 0;
    return false;
}

uint8_t MediaTypeDSK::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::format(uint16_t *responsesize)
{
    return false;
}

mediatype_t MediaTypeDSK::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("DSK MOUNT\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_DSK;
    _media_num_blocks = disksize / MEDIA_BLOCK_SIZE;

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeDSK::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("DSK CREATE\n");

    return true;
}
#endif // BUILD_COCO
