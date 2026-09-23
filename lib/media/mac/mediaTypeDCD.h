#ifndef _MEDIATYPE_DCD_
#define _MEDIATYPE_DCD_

#include <stdio.h>
#include <stdint.h>

#include "global_types.h"
#include "mediaType.h"

/**
 * HD20-style (DCD) block device backed by an image file.
 *
 * Three kinds of image are accepted; mount() works out which:
 *
 *   volume image  A bare HFS (or MFS) volume: block 0 is the boot block,
 *                 block 2 the master directory block. The whole file is
 *                 presented to the Mac as the disk.
 *
 *   drive image   A whole-disk image with an Apple Driver Descriptor
 *                 Record ("ER") in block 0 and an Apple partition map
 *                 ("PM") from block 1. The first Apple_HFS partition is
 *                 located and only that partition is presented to the Mac
 *                 as the disk. Drivers in the image are never used; the
 *                 HD20 protocol has no place for them.
 *
 *   DiskCopy 4.2  An .image file with the 84-byte DC42 header; the data
 *                 fork after the header is treated as a volume image.
 */
class MediaTypeDCD : public MediaType
{
private:
    uint32_t last_block_num = 0xFFFFFFFF;
    uint32_t offset = 0;   // byte offset of block 0 of the presented volume

    bool readonly = false; // set by the device before mount(); see set_readonly()

    bool read_raw(uint32_t byte_offset, uint8_t *buffer, size_t len);
    bool find_hfs_partition(uint32_t &start_block, uint32_t &block_count);
    void check_hfs_volume();
    void check_blessed();

    // Write-behind cache for runs of consecutive sectors, allocated in PSRAM
    // on first write
    static constexpr uint32_t WCACHE_BLOCKS = 64;   // 64 * 512 = 32 KB
    static constexpr unsigned long WCACHE_IDLE_MS = 300;

    uint8_t *wcache = nullptr;          // WCACHE_BLOCKS * DCD_BLOCK_SIZE bytes, PSRAM
    uint32_t wcache_first = 0;          // first block number held in wcache
    uint32_t wcache_count = 0;          // contiguous blocks currently held (0 = empty)
    unsigned long wcache_last_write_ms = 0;
    bool wcache_error = false;          // a previous flush() failed; fail writes until cleared

    success_is_true wcache_alloc();
    error_is_true write_direct(uint32_t blockNum, uint8_t *buffer);

public:
    enum class image_kind_t { UNKNOWN, VOLUME, DRIVE, DC42 };
    image_kind_t image_kind = image_kind_t::UNKNOWN;

    virtual bool read(uint32_t blockNum, uint8_t* buffer) override;
    virtual bool write(uint32_t blockNum,  uint8_t* buffer) override;
    virtual void unmount() override;

    virtual bool format(uint16_t *responsesize) override;

    virtual mediatype_t mount(FILE *f, uint32_t disksize) override;
    mediatype_t mount(FILE *f) { return mount(f, 0); };

    // Before mount(): a read-only mount never blesses the volume
    void set_readonly(bool ro) { readonly = ro; }

    virtual bool status() override {return (_media_fileh != nullptr);}

    size_t size() {return _media_num_sectors;}
    size_t sectorsize() {return _media_sector_size;}
    void reset_seek_opto() {last_block_num = 0xFFFFFFFF;};

    // Write out the cached run; called on unmount, on status requests and when idle
    error_is_true flush();

    void flush_if_idle();

    // x != 0 forces a fixed data offset (used for DiskCopy 4.2 images)
    MediaTypeDCD(int x = 0) : offset(x) { if (x) image_kind = image_kind_t::DC42; }
    ~MediaTypeDCD();
};


#endif // _MEDIATYPE_DCD_
