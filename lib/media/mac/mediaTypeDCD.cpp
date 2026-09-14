#ifdef BUILD_MAC

#include "mediaTypeDCD.h"

#include <cstring>
#include "utils.h"
#include "../../include/debug.h"

#define DCD_BLOCK_SIZE 512

// Apple Driver Descriptor Record (block 0 of a drive image)
#define DDR_SIGNATURE 0x4552 // 'ER'
// Apple partition map entry (block 1.. of a drive image)
#define PM_SIGNATURE 0x504D // 'PM'
// HFS master directory block signature (block 2 of a volume)
#define HFS_SIGNATURE 0x4244 // 'BD'
// MFS volume information signature (block 2 of a volume)
#define MFS_SIGNATURE 0xD2D7

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool MediaTypeDCD::read(uint32_t blockNum, uint8_t* buffer)
{
    size_t readsize = DCD_BLOCK_SIZE;

    if (blockNum >= _media_num_sectors)
    {
        Debug_printf("\r\nDCD read of block %lu beyond end of disk (%lu blocks)",
                     (unsigned long)blockNum, (unsigned long)_media_num_sectors);
        reset_seek_opto();
        return true;
    }

    if ((blockNum == 0) || (blockNum != last_block_num + 1)) // only seek if not reading next block
    {
        if (fseek(_media_fileh, (blockNum * readsize) + offset, SEEK_SET))
        {
            reset_seek_opto();
            return true;
        }
    }

    last_block_num = blockNum;

    readsize = fread((unsigned char *)buffer, 1, readsize, _media_fileh);
    if (readsize != _media_sector_size)
        reset_seek_opto();
    return (readsize != _media_sector_size);
}

bool MediaTypeDCD::write(uint32_t blockNum, uint8_t* buffer)
{
    size_t writesize = DCD_BLOCK_SIZE;

    if (blockNum >= _media_num_sectors)
    {
        Debug_printf("\r\nDCD write of block %lu beyond end of disk (%lu blocks)",
                     (unsigned long)blockNum, (unsigned long)_media_num_sectors);
        reset_seek_opto();
        return true;
    }

    if (blockNum != last_block_num + 1) // only seek if not writing next block
    {
        if (fseek(_media_fileh, (blockNum * writesize) + offset, SEEK_SET))
        {
            reset_seek_opto();
            return true;
        }
    }
    last_block_num = blockNum;
    writesize = fwrite((unsigned char *)buffer, 1, writesize, _media_fileh);
    if (writesize != _media_sector_size)
    {
        reset_seek_opto();
        return true;
    }

    return false;
}

bool MediaTypeDCD::format(uint16_t *responsesize)
{
    return false;
}

// Read len bytes at an absolute byte offset in the file. Returns true on error.
bool MediaTypeDCD::read_raw(uint32_t byte_offset, uint8_t *buffer, size_t len)
{
    if (fseek(_media_fileh, byte_offset, SEEK_SET))
        return true;
    return fread(buffer, 1, len, _media_fileh) != len;
}

// Walk the Apple partition map of a drive image and return the first
// Apple_HFS partition. Returns false if there is none.
bool MediaTypeDCD::find_hfs_partition(uint32_t &start_block, uint32_t &block_count)
{
    uint8_t blk[DCD_BLOCK_SIZE];
    uint32_t map_entries = 1;

    for (uint32_t i = 1; i <= map_entries && i < 64; i++)
    {
        if (read_raw(i * DCD_BLOCK_SIZE, blk, sizeof(blk)))
            return false;
        if (be16(&blk[0]) != PM_SIGNATURE)
        {
            Debug_printf("\r\nDCD: partition map entry %lu has bad signature %04x", (unsigned long)i, be16(&blk[0]));
            return false;
        }
        if (i == 1)
            map_entries = be32(&blk[4]); // pmMapBlkCnt

        uint32_t pstart = be32(&blk[8]);  // pmPyPartStart
        uint32_t pcount = be32(&blk[12]); // pmPartBlkCnt
        char pname[33], ptype[33];
        memcpy(pname, &blk[16], 32); pname[32] = 0;
        memcpy(ptype, &blk[48], 32); ptype[32] = 0;
        Debug_printf("\r\nDCD: partition %lu \"%s\" type %s start %lu count %lu",
                     (unsigned long)i, pname, ptype, (unsigned long)pstart, (unsigned long)pcount);

        if (strcmp(ptype, "Apple_HFS") == 0)
        {
            start_block = pstart;
            block_count = pcount;
            return true;
        }
    }
    return false;
}

// Sanity-check the volume we are about to present: compare what the HFS
// master directory block says the volume size is with what we have.
void MediaTypeDCD::check_hfs_volume()
{
    uint8_t mdb[DCD_BLOCK_SIZE];

    if (read_raw(offset + 2 * DCD_BLOCK_SIZE, mdb, sizeof(mdb)))
    {
        Debug_printf("\r\nDCD: could not read volume header");
        return;
    }

    uint16_t sig = be16(&mdb[0]);
    if (sig == MFS_SIGNATURE)
    {
        Debug_printf("\r\nDCD: MFS volume");
        return;
    }
    if (sig != HFS_SIGNATURE)
    {
        Debug_printf("\r\nDCD: no HFS/MFS signature in block 2 (%04x); the Mac will probably call this disk damaged", sig);
        return;
    }

    uint16_t nm_al_blks = be16(&mdb[18]);   // drNmAlBlks
    uint32_t al_blk_siz = be32(&mdb[20]);   // drAlBlkSiz (bytes)
    uint16_t al_bl_st = be16(&mdb[28]);     // drAlBlSt (first allocation block, in 512 byte blocks)
    uint8_t name_len = mdb[36] > 27 ? 27 : mdb[36];
    char name[28];
    memcpy(name, &mdb[37], name_len); name[name_len] = 0;

    // allocation area + alternate MDB and boot-block-sized tail (2 blocks)
    uint32_t needed = al_bl_st + nm_al_blks * (al_blk_siz / DCD_BLOCK_SIZE) + 2;

    Debug_printf("\r\nDCD: HFS volume \"%s\": %u alloc blocks x %lu bytes, needs %lu blocks, image provides %lu",
                 name, nm_al_blks, (unsigned long)al_blk_siz, (unsigned long)needed, (unsigned long)_media_num_sectors);

    if (needed > _media_num_sectors)
        Debug_printf("\r\nDCD: WARNING image is truncated (short by %lu blocks); the Mac will report it as damaged",
                     (unsigned long)(needed - _media_num_sectors));
}

mediatype_t MediaTypeDCD::mount(FILE *f, uint32_t disksize)
{
    uint8_t blk0[DCD_BLOCK_SIZE];

    _media_fileh = f;
    _media_sector_size = DCD_BLOCK_SIZE;

    if (image_kind == image_kind_t::DC42)
    {
        // DiskCopy 4.2: data size lives in the header, data starts at `offset`
        uint8_t hdr[0x54];
        if (!read_raw(0, hdr, sizeof(hdr)))
        {
            uint32_t data_size = be32(&hdr[0x40]);
            if (data_size && data_size + offset <= disksize)
                disksize = data_size + offset;
        }
        _media_num_sectors = (disksize - offset) / DCD_BLOCK_SIZE;
        Debug_printf("\r\nDCD: DiskCopy 4.2 image, %lu blocks", (unsigned long)_media_num_sectors);
    }
    else if (!read_raw(0, blk0, sizeof(blk0)) && be16(&blk0[0]) == DDR_SIGNATURE)
    {
        // Drive image: present the first HFS partition
        uint32_t pstart = 0, pcount = 0;
        uint16_t ddr_blk_size = be16(&blk0[2]);
        if (ddr_blk_size != DCD_BLOCK_SIZE)
            Debug_printf("\r\nDCD: drive image block size %u, assuming 512", ddr_blk_size);

        if (!find_hfs_partition(pstart, pcount))
        {
            Debug_printf("\r\nDCD: drive image has no Apple_HFS partition - cannot mount");
            _media_fileh = nullptr;
            return MEDIATYPE_UNKNOWN;
        }
        image_kind = image_kind_t::DRIVE;
        offset = pstart * DCD_BLOCK_SIZE;
        _media_num_sectors = pcount;
        uint32_t avail = (disksize > offset) ? (disksize - offset) / DCD_BLOCK_SIZE : 0;
        if (_media_num_sectors > avail)
        {
            Debug_printf("\r\nDCD: partition claims %lu blocks but file only holds %lu; clamping",
                         (unsigned long)_media_num_sectors, (unsigned long)avail);
            _media_num_sectors = avail;
        }
        Debug_printf("\r\nDCD: drive image, presenting HFS partition at block %lu, %lu blocks",
                     (unsigned long)pstart, (unsigned long)_media_num_sectors);
    }
    else
    {
        // Bare volume image
        image_kind = image_kind_t::VOLUME;
        offset = 0;
        _media_num_sectors = disksize / DCD_BLOCK_SIZE;
        Debug_printf("\r\nDCD: volume image, %lu blocks", (unsigned long)_media_num_sectors);
    }

    num_blocks = _media_num_sectors;
    check_hfs_volume();
    reset_seek_opto();
    return MEDIATYPE_DCD;
}

#endif // BUILD_MAC
