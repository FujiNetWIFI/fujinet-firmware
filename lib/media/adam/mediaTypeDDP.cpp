#ifdef BUILD_ADAM

#include "mediaTypeDDP.h"

#include <cstdint>
#include <cstring>

#ifdef ESP_PLATFORM
#include <unistd.h>  // for fsync
#endif

#include "../../include/debug.h"


// Returns byte offset of given sector number
uint32_t MediaTypeDDP::_block_to_offset(uint32_t blockNum)
{
    return blockNum * 1024;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDDP::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        return false; // We already have block.

    Debug_print("DDP READ\r\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks-1)
    {
        Debug_printf("::read block %lu > %lu\r\n", blockNum, _media_num_blocks);
        _media_controller_status=2;
        return true;
    }

    memset(_media_blockbuff, 0, sizeof(_media_blockbuff));

    bool err = false;
    // // Perform a seek if we're not reading the sector after the last one we read
    // if (blockNum != _media_last_block + 1)
    // {
        uint32_t offset = _block_to_offset(blockNum);
        err = fseek(_media_fileh, offset, SEEK_SET) != 0;
        _media_last_block = INVALID_SECTOR_VALUE;
    // }

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

    return err;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDDP::write(uint32_t blockNum, bool verify)
{
    Debug_printf("DDP WRITE [%lu/%lu]\r\n", blockNum, _media_num_blocks);

    uint32_t offset = _block_to_offset(blockNum);

    _media_last_block = INVALID_SECTOR_VALUE;

    if (_media_fileh->_flags == 0x1484) // mounted R/O, attempt HS R/W
    {
        Debug_printf("High score mode activated, attempting write open\r\n");
        
        oldFileh = _media_fileh;
        hsFileh = _media_host->file_open(_disk_filename, _disk_filename, strlen(_disk_filename) + 1, "r+");
        _media_fileh = hsFileh;   
    }

    // Perform a seek if we're writing to the sector after the last one
    int e;
//    if (blockNum != _media_last_block + 1)
//    {
        e = fseek(_media_fileh, offset, SEEK_SET);
        if (e != 0)
        {
            Debug_printf("::write seek error %d\r\n", e);
            _media_controller_status=2;
            return true;
        }
//    }
    // Write the data
    e = fwrite(&_media_blockbuff[0], 1, 256, _media_fileh);
    e += fwrite(&_media_blockbuff[256], 1, 256, _media_fileh);
    e += fwrite(&_media_blockbuff[512], 1, 256, _media_fileh);
    e += fwrite(&_media_blockbuff[768], 1, 256, _media_fileh);
    
    if (e != 1024)
    {
        Debug_printf("::write error %d, %d\r\n", e, errno);
        return true;
    }

    int ret = fflush(_media_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything
    ret = fsync(fileno(_media_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
    Debug_printf("DDP::write fsync:%d\r\n", ret);

    Debug_printv("media flags %x\n",_media_fileh->_flags);
    
    if (_media_fileh->_flags == 0x1484)
    {
        Debug_printf("Closing high score sector.\r\n");

        if (hsFileh != nullptr)
            fclose(hsFileh);

        _media_fileh = oldFileh;
        _media_last_block = INVALID_SECTOR_VALUE; // force a cache invalidate.
    }
    else
        _media_last_block = blockNum;

    _media_last_block = INVALID_SECTOR_VALUE;
    _media_controller_status=0;
    return false;
}

uint8_t MediaTypeDDP::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
bool MediaTypeDDP::format(uint16_t *responsesize)
{
    return false;
}

mediatype_t MediaTypeDDP::mount(FILE *f, uint32_t disksize)
{
    Debug_print("DDP MOUNT\r\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_DDP;
    _media_num_blocks = disksize / 1024;

    Debug_printv("FLAGS: %x\n",_media_fileh->_flags);
    return _mediatype;
}

// Returns FALSE on error
bool MediaTypeDDP::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("DDP CREATE\r\n");

    return true;
}
#endif /* BUILD_ADAM */
