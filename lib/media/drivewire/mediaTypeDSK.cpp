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
error_is_true MediaTypeDSK::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        RETURN_SUCCESS_AS_FALSE(); // We already have block.

    // Debug_print("DW DSK READ\n");

    // Return an error if we're trying to read beyond the end of the disk
    if (blockNum > _media_num_blocks)
    {
        Debug_printf("::read block %lu > %lu\n", blockNum, _media_num_blocks);
        _media_controller_status = 2;
        RETURN_ERROR_AS_TRUE();
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

    RETURN_ERROR_IF(err);
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeDSK::write(uint32_t blockNum, bool verify)
{
    // Debug_printf("DSK WRITE\n", blockNum, _media_num_blocks);

    fnFile *hsFileh = nullptr;
    fnFile *oldFileh = nullptr;

    if (_media_read_only)
    {
        // High-score marked sectors are writable even on a read-only mount,
        // via a temporary read-write handle (see the Atari ATR equivalent).
        if (!_high_score_block(blockNum) || _media_host == nullptr)
        {
            Debug_printf("DSK::write block %lu rejected (read-only)\n", blockNum);
            _media_controller_status = 2;
            RETURN_ERROR_AS_TRUE();
        }

        Debug_printf("DSK::write high score block %lu, opening read-write\n", blockNum);
        hsFileh = _media_host->fnfile_open(_disk_filename, _disk_filename,
                                           strlen(_disk_filename) + 1, "rb+");
        if (hsFileh == nullptr)
        {
            Debug_printf("DSK::write high score open failed\n");
            _media_controller_status = 2;
            RETURN_ERROR_AS_TRUE();
        }
        oldFileh = _media_fileh;
        _media_fileh = hsFileh;
    }

    uint32_t offset = _block_to_offset(blockNum);

    _media_last_block = INVALID_SECTOR_VALUE;

    int e = fnio::fseek(_media_fileh, offset, SEEK_SET);
    bool err = (e != 0);
    if (err)
        Debug_printf("::write seek error %d\n", e);

    if (!err)
    {
        e = fnio::fwrite(&_media_blockbuff, 1, MEDIA_BLOCK_SIZE, _media_fileh);
        err = (e != MEDIA_BLOCK_SIZE);
        if (err)
            Debug_printf("::write error %d, %d\n", e, errno);
    }

    if (!err)
    {
        int ret = fnio::fflush(_media_fileh);    // This doesn't seem to be connected to anything in ESP-IDF VF, so it may not do anything

        // This next line is commented out because there's no fsync in the
        // fnio class. In a discussion with @apc from the following discussion:
        // https://discord.com/channels/655893677146636301/1209535440915406848/1231880068528214026
        // fnio::fflush() should be sufficient for syncing as well.
//      ret = fsync(fileno(_media_fileh)); // Since we might get reset at any moment, go ahead and sync the file (not clear if fflush does this)
        Debug_printf("DSK::write fsync:%d\n", ret);
    }

    if (hsFileh != nullptr)
    {
        fnio::fclose(hsFileh);
        _media_fileh = oldFileh;
    }

    _media_last_block = INVALID_SECTOR_VALUE;

    if (err)
    {
        _media_controller_status = 2;
        RETURN_ERROR_AS_TRUE();
    }

    _media_controller_status = 0;
    RETURN_SUCCESS_AS_FALSE();
}

void MediaTypeDSK::_parse_high_score_marker()
{
    uint8_t buf[MEDIA_BLOCK_SIZE];

    _hs_num_ranges = 0;

    if (_media_num_blocks <= HS_MARKER_BLOCK)
        return;

    if (fnio::fseek(_media_fileh, _block_to_offset(HS_MARKER_BLOCK), SEEK_SET) != 0)
        return;
    if (fnio::fread(buf, 1, MEDIA_BLOCK_SIZE, _media_fileh) != MEDIA_BLOCK_SIZE)
        return;

    if (memcmp(buf, "FNHS", 4) != 0 || buf[4] != 1)
        return;

    uint8_t n = buf[5];
    if (n > HS_MAX_RANGES)
        n = HS_MAX_RANGES;

    for (uint8_t i = 0; i < n; i++)
    {
        _hs_start[i] = (buf[6 + i * 4] << 8) | buf[7 + i * 4];
        _hs_count[i] = (buf[8 + i * 4] << 8) | buf[9 + i * 4];
        Debug_printf("DSK high score sectors: LSN %u, count %u\n",
                     _hs_start[i], _hs_count[i]);
    }
    _hs_num_ranges = n;
}

bool MediaTypeDSK::_high_score_block(uint32_t blockNum)
{
    for (uint8_t i = 0; i < _hs_num_ranges; i++)
        if (blockNum >= _hs_start[i] && blockNum < (uint32_t)(_hs_start[i] + _hs_count[i]))
            return true;
    return false;
}

uint8_t MediaTypeDSK::status()
{
    return _media_controller_status;
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeDSK::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

mediatype_t MediaTypeDSK::mount(fnFile *f, uint32_t disksize)
{
    Debug_print("DSK MOUNT\n");

    _media_fileh = f;
    _mediatype = MEDIATYPE_DSK;
    _media_num_blocks = disksize / MEDIA_BLOCK_SIZE;

    _parse_high_score_marker();

    return _mediatype;
}

// Returns FALSE on error
success_is_true MediaTypeDSK::create(FILE *f, uint32_t numBlocks)
{
    Debug_print("DSK CREATE\n");

    RETURN_ERROR_AS_FALSE();
}
#endif // BUILD_COCO
