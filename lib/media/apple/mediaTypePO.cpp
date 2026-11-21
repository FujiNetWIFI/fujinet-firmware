#ifdef BUILD_APPLE

#include "mediaTypePO.h"

#include <cstring>
#include "utils.h"
#include "../../include/debug.h"

error_is_true MediaTypePO::read(uint32_t blockNum, uint16_t *count, uint8_t* buffer)
{
    size_t readsize = *count;
if (blockNum == 0 || blockNum != last_block_num + 1) // example optimization, only do seek if not reading next block -tschak
  {
     if (fnio::fseek(_media_fileh, (blockNum * readsize) + offset, SEEK_SET))
    {
        reset_seek_opto();
        RETURN_ERROR_AS_TRUE();
    }
  }

  if (high_score_enabled && blockNum >= _high_score_block_lb && blockNum <= _high_score_block_ub)
    last_block_num = INVALID_SECTOR_VALUE; // try to invalidate cache if game re-reads hs table
  else
    last_block_num = blockNum;

  readsize = fnio::fread((unsigned char *)buffer, 1, readsize, _media_fileh);
  RETURN_ERROR_IF(readsize != *count);
}

error_is_true MediaTypePO::write(uint32_t blockNum, uint16_t *count, uint8_t* buffer)
{
    size_t writesize = *count;
    const bool high_score_block = high_score_enabled && blockNum >= _high_score_block_lb && blockNum <= _high_score_block_ub;

    if (blockNum >= num_blocks)
        RETURN_ERROR_AS_TRUE();

    if (high_score_block)
    {
        Debug_printf("high score: Swapping file handles\r\n");
        hsFileh = _media_host->fnfile_open(_disk_filename, _disk_filename, strlen(_disk_filename) +1, "rb+");
        if (hsFileh == nullptr)
            RETURN_ERROR_AS_TRUE();
        oldFileh = _media_fileh;
        _media_fileh = hsFileh;
    }

    error_is_true err(false);

    if (high_score_block || blockNum != last_block_num + 1) // example optimization, only do seek if not writing next block -tschak
    {
        if (fnio::fseek(_media_fileh, (blockNum * writesize) + offset, SEEK_SET))
            err = error_is_true(true);
    }
    if (err.is_success())
    {
        last_block_num = blockNum;
        if (fnio::fwrite((unsigned char *)buffer, 1, writesize, _media_fileh) != *count)
            err = error_is_true(true);
    }
    if (err.is_error())
        reset_seek_opto();

    if (high_score_block)
    {
        Debug_printf("high score: Reverting file handles.\r\n");
        fnio::fclose(hsFileh);
        hsFileh = nullptr;
        _media_fileh = oldFileh;
        last_block_num = INVALID_SECTOR_VALUE; // Invalidate cache
    }

    return err;
}

error_is_true MediaTypePO::write_sector(int track, int sector, uint8_t *buffer)
{
  Debug_printf("\r\nProDOS disk needs to write sector!");
  RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypePO::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

mediatype_t MediaTypePO::mount(fnFile *f, uint32_t disksize)
{
    diskiiemulation = false;
    _media_fileh = f;
    num_blocks = disksize / 512;
    return MEDIATYPE_PO;
}

#endif // BUILD_APPLE
