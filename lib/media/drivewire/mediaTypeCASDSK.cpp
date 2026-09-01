#ifdef BUILD_COCO

#include "mediaTypeCASDSK.h"

#include <cstdint>
#include <cstring>

#include "../../include/debug.h"

mediatype_t MediaTypeCASDSK::mount(fnFile *f, uint32_t disksize)
{
    Debug_printf("DW CASDSK MOUNT %s (%lu bytes)\n", _disk_filename,
                 (unsigned long)disksize);

    _media_fileh = f;
    _mediatype = MEDIATYPE_CAS;
    _media_image_size = disksize;
    _media_read_only = true;
    _media_last_block = INVALID_SECTOR_VALUE;
    _ready = false;

    _source.attach(f, disksize);

    if (!_index.build(&_reader))
    {
        Debug_printv("CASDSK: no files found in image");
        return MEDIATYPE_UNKNOWN;
    }

    // Disk name comes from the image's own filename.
    char vol[9] = {0};
    const char *base = strrchr(_disk_filename, '/');
    base = base ? base + 1 : _disk_filename;
    for (int i = 0; i < 8 && base[i] && base[i] != '.'; i++)
        vol[i] = base[i];

    if (!_layout.build(&_index, vol))
    {
        Debug_printv("CASDSK: nothing could be placed on the disk");
        return MEDIATYPE_UNKNOWN;
    }

    // Always a full 35 track disk, whatever the size of the tape.
    _media_num_blocks = DECB_TOTAL_SECTORS;

    Debug_printf("CASDSK: %d file(s), %lu granule(s), index %lu bytes\n",
                 _layout.file_count(), (unsigned long)_layout.granules_used(),
                 (unsigned long)_index.index_bytes());
    if (_layout.dropped_full())
        Debug_printf("CASDSK: %d file(s) dropped, disk full (%d granules, %d entries max)\n",
                     _layout.dropped_full(), DECB_MAX_GRANULES, DECB_MAX_DIRENTS);
    if (_index.noise_bytes())
        Debug_printf("CASDSK: ignored %lu bytes of trailing noise\n",
                     (unsigned long)_index.noise_bytes());

    _ready = true;
    return _mediatype;
}

void MediaTypeCASDSK::unmount()
{
    _ready = false;
    MediaType::unmount();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeCASDSK::read(uint32_t blockNum, uint16_t *readcount)
{
    if (blockNum == _media_last_block)
        RETURN_SUCCESS_AS_FALSE(); // We already have block.

    if (!_ready || blockNum >= _media_num_blocks)
    {
        Debug_printf("CASDSK::read block %lu out of range\n", (unsigned long)blockNum);
        _media_controller_status = 2;
        RETURN_ERROR_AS_TRUE();
    }

    if (!_layout.read_sector(blockNum, _media_blockbuff))
    {
        _media_last_block = INVALID_SECTOR_VALUE;
        _media_controller_status = 2;
        RETURN_ERROR_AS_TRUE();
    }

    _media_last_block = blockNum;
    _media_controller_status = 0;
    RETURN_SUCCESS_AS_FALSE();
}

// Returns TRUE if an error condition occurred
error_is_true MediaTypeCASDSK::write(uint32_t blockNum, bool verify)
{
    // There is nowhere in a cassette image to put a changed sector.
    Debug_printf("CASDSK::write block %lu rejected (read-only format)\n",
                 (unsigned long)blockNum);
    _media_controller_status = 2;
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaTypeCASDSK::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

uint8_t MediaTypeCASDSK::status()
{
    return _media_controller_status;
}

#endif // BUILD_COCO
