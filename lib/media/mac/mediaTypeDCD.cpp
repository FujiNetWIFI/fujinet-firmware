#ifdef BUILD_MAC

#include "mediaTypeDCD.h"

#include <cstring>
#include <esp_heap_caps.h>
#include "fnSystem.h"
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

    // the Mac reads a written block straight back to verify it
    if (wcache_count > 0 && blockNum >= wcache_first && blockNum < wcache_first + wcache_count)
    {
        memcpy(buffer, wcache + (blockNum - wcache_first) * DCD_BLOCK_SIZE, DCD_BLOCK_SIZE);
        return false;
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

// Uncached single-sector write, used when the cache could not be allocated
error_is_true MediaTypeDCD::write_direct(uint32_t blockNum, uint8_t *buffer)
{
    size_t writesize = DCD_BLOCK_SIZE;

    if (blockNum != last_block_num + 1) // only seek if not writing next block
    {
        if (fseek(_media_fileh, (blockNum * writesize) + offset, SEEK_SET))
        {
            reset_seek_opto();
            RETURN_ERROR_AS_TRUE();
        }
    }
    last_block_num = blockNum;
    writesize = fwrite((unsigned char *)buffer, 1, writesize, _media_fileh);
    if (writesize != _media_sector_size)
    {
        reset_seek_opto();
        RETURN_ERROR_AS_TRUE();
    }

    RETURN_SUCCESS_AS_FALSE();
}

success_is_true MediaTypeDCD::wcache_alloc()
{
    if (wcache != nullptr)
        RETURN_SUCCESS_AS_TRUE();

    wcache = static_cast<uint8_t *>(heap_caps_malloc(WCACHE_BLOCKS * DCD_BLOCK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (wcache == nullptr)
        Debug_printf("\r\nDCD: write-behind cache allocation failed, using direct writes");
    RETURN_SUCCESS_IF(wcache != nullptr);
}

bool MediaTypeDCD::write(uint32_t blockNum, uint8_t* buffer)
{
    if (blockNum >= _media_num_sectors)
    {
        Debug_printf("\r\nDCD write of block %lu beyond end of disk (%lu blocks)",
                     (unsigned long)blockNum, (unsigned long)_media_num_sectors);
        reset_seek_opto();
        return true;
    }

    if (wcache_error)
    {
        Debug_printf("\r\nDCD write of block %lu refused after earlier flush failure", (unsigned long)blockNum);
        return true;
    }

    if (wcache == nullptr && wcache_alloc().is_error())
        return write_direct(blockNum, buffer).is_error();

    // overwrite a block already inside the cached run
    if (wcache_count > 0 && blockNum >= wcache_first && blockNum < wcache_first + wcache_count)
    {
        memcpy(wcache + (blockNum - wcache_first) * DCD_BLOCK_SIZE, buffer, DCD_BLOCK_SIZE);
        wcache_last_write_ms = fnSystem.millis();
        return false;
    }

    // empty cache, or a sequential append onto the current run
    if (wcache_count == 0 || (blockNum == wcache_first + wcache_count && wcache_count < WCACHE_BLOCKS))
    {
        if (wcache_count == 0)
            wcache_first = blockNum;
        memcpy(wcache + wcache_count * DCD_BLOCK_SIZE, buffer, DCD_BLOCK_SIZE);
        wcache_count++;
        wcache_last_write_ms = fnSystem.millis();
        return false;
    }

    // non-sequential, or the run is full: write it out and start a new run
    if (flush().is_error())
    {
        wcache_error = true;
        return true;
    }

    wcache_first = blockNum;
    memcpy(wcache, buffer, DCD_BLOCK_SIZE);
    wcache_count = 1;
    wcache_last_write_ms = fnSystem.millis();
    return false;
}

error_is_true MediaTypeDCD::flush()
{
    if (wcache_count == 0)
        RETURN_SUCCESS_AS_FALSE();

    unsigned long t0 = fnSystem.millis();
    uint32_t first = wcache_first;
    uint32_t count = wcache_count;
    bool err = false;

    if (fseek(_media_fileh, ((size_t)first * DCD_BLOCK_SIZE) + offset, SEEK_SET))
        err = true;
    else
    {
        // one seek, then one TNFS write per sector: fflush each, since TNFS
        // rejects a write over 525 bytes and stdio would merge sectors
        for (uint32_t i = 0; i < count && !err; i++)
        {
            if (fwrite(wcache + (size_t)i * DCD_BLOCK_SIZE, 1, DCD_BLOCK_SIZE, _media_fileh) != DCD_BLOCK_SIZE ||
                fflush(_media_fileh) != 0)
                err = true;
        }
        if (!err && fflush(_media_fileh) != 0)
            err = true;
    }

    unsigned long elapsed = fnSystem.millis() - t0;
    Debug_printf("\r\nDCD: flush blocks %lu..%lu (%lu bytes) took %lu ms%s",
                 (unsigned long)first, (unsigned long)(first + count - 1),
                 (unsigned long)count * DCD_BLOCK_SIZE, elapsed, err ? " FAILED" : "");

    wcache_count = 0;
    wcache_error = err;
    reset_seek_opto(); // file position no longer matches last_block_num
    RETURN_ERROR_IF(err);
}

void MediaTypeDCD::flush_if_idle()
{
    if (wcache_count == 0)
        return;
    if (fnSystem.millis() - wcache_last_write_ms >= WCACHE_IDLE_MS)
        flush();
}

void MediaTypeDCD::unmount()
{
    flush();
    MediaType::unmount();
}

MediaTypeDCD::~MediaTypeDCD()
{
    if (wcache != nullptr)
    {
        heap_caps_free(wcache);
        wcache = nullptr;
    }
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
    if (sig == MFS_SIGNATURE || sig == HFS_SIGNATURE)
    {
        // drVN sits at offset 36 in both the MFS and the HFS header
        uint8_t vn_len = mdb[36] > 27 ? 27 : mdb[36];
        memcpy(volume_name, &mdb[37], vn_len);
        volume_name[vn_len] = 0;
    }
    if (sig == MFS_SIGNATURE)
    {
        fs_kind = fs_kind_t::MFS;
        Debug_printf("\r\nDCD: MFS volume");
        return;
    }
    if (sig != HFS_SIGNATURE)
    {
        Debug_printf("\r\nDCD: no HFS/MFS signature in block 2 (%04x); the Mac will probably call this disk damaged", sig);
        return;
    }
    fs_kind = fs_kind_t::HFS;

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

    truncated = needed > _media_num_sectors;
    if (truncated)
        Debug_printf("\r\nDCD: WARNING image is truncated (short by %lu blocks); the Mac will report it as damaged",
                     (unsigned long)(needed - _media_num_sectors));
}

// A Mac only boots a volume whose MDB drFndrInfo[0] names the System Folder;
// image tools often leave it zero. Find the root "System Folder" in the
// catalog and, on a writable mount, record it in both MDBs.
void MediaTypeDCD::check_blessed()
{
    uint8_t blk[DCD_BLOCK_SIZE];

    if (read_raw(offset + 2 * DCD_BLOCK_SIZE, blk, sizeof(blk)))
        return;
    if (be16(&blk[0]) != HFS_SIGNATURE)
        return; // MFS or damaged; check_hfs_volume() already logged it

    uint32_t fndr_info0 = be32(&blk[92]); // drFndrInfo[0], MDB offset 0x5C
    if (fndr_info0 != 0)
    {
        boot = boot_t::BLESSED;
        Debug_printf("\r\nDCD: HFS volume blessed, System Folder id %lu", (unsigned long)fndr_info0);
        return;
    }

    uint32_t al_blk_siz = be32(&blk[20]);            // drAlBlkSiz (bytes)
    uint16_t al_bl_st = be16(&blk[28]);               // drAlBlSt
    uint16_t ext_start = be16(&blk[150]);             // drCTExtRec[0].startBlock, MDB offset 0x96
    uint16_t ext_count = be16(&blk[152]);             // drCTExtRec[0].blockCount, MDB offset 0x98
    uint32_t blocks_per_alloc = al_blk_siz / DCD_BLOCK_SIZE;

    uint32_t cat_block = al_bl_st + (uint32_t)ext_start * blocks_per_alloc;
    uint32_t scan_blocks = ext_count;
    uint32_t cap = (1024UL * 1024UL) / DCD_BLOCK_SIZE; // bound the scan to 1 MB
    if (scan_blocks > cap)
        scan_blocks = cap;

    static const uint8_t key[5] = {0x00, 0x00, 0x00, 0x02, 0x0D}; // parent id 2 (root), name len 13
    uint32_t sysfolder_id = 0;

    for (uint32_t i = 0; i < scan_blocks && sysfolder_id == 0; i++)
    {
        if (read_raw(offset + (cat_block + i) * DCD_BLOCK_SIZE, blk, sizeof(blk)))
            break;
        for (uint32_t p = 2; p + 18 <= DCD_BLOCK_SIZE; p++) // 5-byte key + 13-byte "System Folder"
        {
            if (memcmp(&blk[p], key, sizeof(key)) != 0 || memcmp(&blk[p + 5], "System Folder", 13) != 0)
                continue;

            uint8_t key_len = blk[p - 2]; // key starts 2 bytes before p: keyLen byte, reserved byte
            uint32_t rec = (p - 2) + 1 + key_len;
            rec = (rec + 1) & ~1u; // record starts on an even offset
            if (rec + 10 > DCD_BLOCK_SIZE)
                continue;
            if (blk[rec] != 1) // record type 1 = directory
                continue;
            sysfolder_id = be32(&blk[rec + 6]);
            break;
        }
    }

    if (sysfolder_id == 0)
    {
        boot = boot_t::NO_SYSTEM;
        Debug_printf("\r\nDCD: HFS volume has no System Folder at the root (not bootable)");
        return;
    }

    boot = boot_t::UNBLESSED;
    if (readonly)
    {
        Debug_printf("\r\nDCD: HFS volume is not blessed (System Folder id %lu); mount read/write to fix",
                     (unsigned long)sysfolder_id);
        return;
    }

    auto set_finder_info = [sysfolder_id](uint8_t *b)
    {
        b[92] = (sysfolder_id >> 24) & 0xFF;
        b[93] = (sysfolder_id >> 16) & 0xFF;
        b[94] = (sysfolder_id >> 8) & 0xFF;
        b[95] = sysfolder_id & 0xFF;
        b[96] = 0; b[97] = 0; b[98] = 0; b[99] = 2; // drFndrInfo[2] = 2
    };

    if (read_raw(offset + 2 * DCD_BLOCK_SIZE, blk, sizeof(blk)))
        return;
    set_finder_info(blk);
    if (write(2, blk))
    {
        Debug_printf("\r\nDCD: failed writing primary MDB while blessing volume");
        return;
    }

    if (_media_num_sectors >= 2)
    {
        uint32_t alt_block = _media_num_sectors - 2;
        if (!read(alt_block, blk))
        {
            set_finder_info(blk);
            write(alt_block, blk);
        }
    }

    flush();
    boot = boot_t::BLESSED_NOW;
    Debug_printf("\r\nDCD: HFS volume was not blessed: blessed System Folder id %lu", (unsigned long)sysfolder_id);
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
    check_blessed();
    reset_seek_opto();
    return MEDIATYPE_DCD;
}

#endif // BUILD_MAC
