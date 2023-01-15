#ifdef BUILD_APPLE

#include "mediaTypePO.h"

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
  last_block_num = blockNum;
  readsize = fread((unsigned char *)buffer, 1, readsize, _media_fileh); // Reading block from SD Card
  return (readsize != *count);
}

bool MediaTypePO::write(uint32_t blockNum, uint16_t *count, uint8_t* buffer)
{
    size_t writesize = *count;
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
    return false;
}

bool MediaTypePO::format(uint16_t *respopnsesize)
{
    return false;
}

mediatype_t MediaTypePO::mount(FILE *f, uint32_t disksize)
{
    diskiiemulation = false;
    char hdr[4];
    fread(&hdr,sizeof(char),4,f);
    if (hdr[0] == '2' && hdr[1] == 'I' && hdr[2] == 'M' && hdr[3] == 'G')
        offset = 64;
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