#ifdef BUILD_APPLE

#include "mediaTypePO.h"

#include <cstring>
#include "utils.h"
#include "../../include/debug.h"

bool MediaTypePO::read(uint32_t blockNum, uint16_t *count, uint8_t* buffer)
{
    size_t readsize = *count;
if (blockNum == 0 || blockNum != last_block_num + 1) // example optimization, only do seek if not reading next block -tschak
  {
     if (fseek(_media_fileh, (blockNum * readsize) + offset, SEEK_SET))
    {
        reset_seek_opto();
        return true;
    }
  }

  if (high_score_enabled && _high_score_block == blockNum)
    last_block_num = INVALID_SECTOR_VALUE; // try to invalidate cache if game re-reads hs table
  else
    last_block_num = blockNum;

  readsize = fread((unsigned char *)buffer, 1, readsize, _media_fileh); // Reading block from SD Card
  return (readsize != *count);
}

bool MediaTypePO::write(uint32_t blockNum, uint16_t *count, uint8_t* buffer)
{
    size_t writesize = *count;

    if (high_score_enabled && blockNum == _high_score_block)
    {
        Debug_printf("high score: Swapping file handles\r\n");
        oldFileh = _media_fileh;
        hsFileh = _media_host->file_open(_disk_filename, _disk_filename, strlen(_disk_filename) +1, "r+");
        _media_fileh = hsFileh;
    }

    if (blockNum != last_block_num + 1) // example optimization, only do seek if not writing next block -tschak
    {
         if (fseek(_media_fileh, (blockNum * writesize) + offset, SEEK_SET))
        {
            reset_seek_opto();
            return true;
        }
    }
    last_block_num = blockNum;
    writesize = fwrite((unsigned char *)buffer, 1, writesize, _media_fileh);
    if (writesize != *count)
    {
       reset_seek_opto();
       return true;
    }

    if (high_score_enabled && blockNum == _high_score_block)
    {
        Debug_printf("high score: Reverting file handles.\r\n");
        if (hsFileh != nullptr)
            fclose(hsFileh);

        _media_fileh = oldFileh;
        last_block_num = INVALID_SECTOR_VALUE; // Invalidate cache
    }

    return false;
}

bool MediaTypePO::format(uint16_t *respopnsesize)
{
    return false;
}

mediatype_t MediaTypePO::mount(FILE *f, uint32_t disksize)
{
    diskiiemulation = false;
    char hdr[64];
    fread(&hdr,sizeof(char),64,f);
    if (hdr[0] == '2' && hdr[1] == 'I' && hdr[2] == 'M' && hdr[3] == 'G')
    {
        // check for 'high score enabled' signature
        if (hdr[48] == 'H' && hdr[49] == 'I')
        {
            _high_score_block = UINT16_FROM_HILOBYTES(hdr[51], hdr[50]);
            if (_high_score_block > 0)
            {
                Debug_printf("high score: Requested block: 0x%04x\r\n", _high_score_block);
                high_score_enabled = true;
            }
        }
        offset = 64;
    }
  _media_fileh = f;
  disksize -= offset;
  num_blocks = disksize/512;
  return MEDIATYPE_PO;
}


// static bool create(FILE *f, uint32_t numBlock)
// {
//     return false;
// }

#endif // BUILD_APPLE
