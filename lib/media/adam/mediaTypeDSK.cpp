#ifdef BUILD_ADAM
#include <memory.h>
#include <string.h>

#include "../../include/debug.h"
#include "../utils/utils.h"

#include "fnSystem.h"
#include "../device/adamnet/disk.h"

#include "mediaTypeDSK.h"

#define INTERLEAVE 5 // 5:1 sector layout in image files (WHY?!?!!?)

// Returns byte offsets of given block number, with interleave
std::pair<uint32_t, uint32_t> MediaTypeDSK::_block_to_offsets(uint32_t blockNum)
{
    int r = blockNum % 4;
    uint32_t o1, o2;

    o1 = blockNum * 1024;
    o2 = ((r == 0) || (r == 1)) ? (blockNum * 1024) + (INTERLEAVE * 512)
                                : (blockNum * 1024) - (4096 - (INTERLEAVE * 512));

    return std::make_pair(o1,o2);
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::read(uint32_t blockNum, uint16_t *readcount)
{
    Debug_print("DSK READ\n");

    if (blockNum == _media_last_block)
        return false; // Already have

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks)
    {
        Debug_printf("::read block %d > %d\n", blockNum, _media_num_blocks);
        return true;
    }

    memset(_media_blockbuff, 0, sizeof(_media_blockbuff));

    bool err = false;

    // Read lower part of block    
    std::pair <uint32_t, uint32_t> offsets = _block_to_offsets(blockNum);
    err = fseek(_media_fileh, offsets.first, SEEK_SET) != 0;

    if (err == false)
        err = fread(_media_blockbuff, 1, 512, _media_fileh) != 512;

   // Read upper part of block
    if (err == false)
        err = fseek(_media_fileh, offsets.second, SEEK_SET) != 0;

    if (err == false)
        err = fread(&_media_blockbuff[512],1,512,_media_fileh) != 512;

    if (err == false)
        _media_last_block = blockNum;
    else
        _media_last_block = INVALID_SECTOR_VALUE;

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::write(uint32_t blockNum, bool verify)
{
    bool err = false;
    Debug_println("DSK WRITE");

    std::pair <uint32_t, uint32_t> offsets = _block_to_offsets(blockNum);

    // Write lower part of block
    err = fseek(_media_fileh, offsets.first, SEEK_SET) != 0;
    if (err == false)
        err = fwrite(_media_blockbuff,1,512,_media_fileh) != 512;
    
    // Write upper part of block
    if (err == false)
        err = fseek(_media_fileh, offsets.second, SEEK_SET) != 0;
    if (err == false)
        err = fwrite(&_media_blockbuff[512],1,512,_media_fileh) != 512;

    int ret = fflush(_media_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_media_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("DSK::write fsync:%d\n", ret);

    return false;
}

void MediaTypeDSK::status(uint8_t statusbuff[4])
{
    // Currently not being used.
}

// Returns TRUE if an error condition occurred
bool MediaTypeDSK::format(uint16_t *responsesize)
{
    memset(_media_blockbuff,0xE5,1024);
    for (uint32_t b=0;b<_media_num_blocks;b++)
        write(b,0);
    return false;
}

mediatype_t MediaTypeDSK::mount(FILE *f, uint32_t disksize)
{
    Debug_print("DSK MOUNT\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_DSK;
    _media_num_blocks = disksize / 1024;

    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeDSK::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("DSK CREATE\n");
    uint8_t buf[1024];

    memset(buf,0xE5,1024);
    for (uint32_t b=0; b<numBlocks; b++)
        fwrite(buf,1024,1,f);

    return true;
}
#endif /* BUILD_ADAM */
