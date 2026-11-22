#ifdef BUILD_RS232

#include "LegacyBlockMedia.h"
#include "rs232/diskType.h"

#include <algorithm>

MediaType &LegacyBlockMedia::legacy() const
{
    return const_cast<MediaType &>(static_cast<const MediaType &>(*this));
}

uint8_t LegacyBlockMedia::sideCount() const
{
    return legacy()._percomBlock.num_sides + 1;
}

// A PERCOM track count of 0 stands for more than 255.
uint16_t LegacyBlockMedia::cylinderCount() const
{
    if (legacy()._percomBlock.num_tracks != 0)
        return legacy()._percomBlock.num_tracks;
    uint32_t perCylinder = legacy()._percomBlock.sectors_per_track * sideCount();
    return perCylinder ? (blockCount() + perCylinder - 1) / perCylinder : 0;
}

TrackGeometry LegacyBlockMedia::trackGeometry(uint16_t /*cylinder*/, uint8_t /*side*/) const
{
    const auto &percom = legacy()._percomBlock;
    return {static_cast<uint8_t>(percom.sectors_per_track), percom.sector_size,
            percom.density == DENSITY_MFM ? TrackEncoding::MFM : TrackEncoding::FM};
}

uint32_t LegacyBlockMedia::blockCount() const
{
    return legacy()._disk_num_sectors;
}

uint16_t LegacyBlockMedia::blockSize(uint32_t block) const
{
    return block < blockCount() ? legacy().sector_size(block) : 0;
}

error_is_true LegacyBlockMedia::readBlock(uint32_t block, ByteBuffer &buffer)
{
    uint32_t count = 0;
    if (block >= blockCount() || legacy().read(block, &count).is_error())
        RETURN_ERROR_AS_TRUE();
    buffer.assign(legacy()._disk_sectorbuff, legacy()._disk_sectorbuff + count);
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true LegacyBlockMedia::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    if (_readOnly || buffer.size() != blockSize(block)
        || buffer.size() > sizeof(legacy()._disk_sectorbuff))
        RETURN_ERROR_AS_TRUE();
    std::copy(buffer.begin(), buffer.end(), legacy()._disk_sectorbuff);
    return legacy().write(block, false);
}

error_is_true LegacyBlockMedia::close()
{
    legacy().unmount();
    _fileh = nullptr;
    RETURN_SUCCESS_AS_FALSE();
}

// Accepts only what discover_mediatype() would have picked this class for, with
// anything unrecognized going to the raw image. A refused image gives the handle
// back unclosed, so another format can try it.
error_is_true LegacyBlockMedia::attach()
{
    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    mediatype_t expected = MediaType::discover_mediatype(_fileh, _path.c_str(), _size);
    if (expected == MEDIATYPE_UNKNOWN)
        expected = MEDIATYPE_IMG;
    if (legacy().mount(_fileh, _size) != expected)
    {
        legacy()._disk_fileh = nullptr;
        legacy().unmount();
        RETURN_ERROR_AS_TRUE();
    }
    RETURN_SUCCESS_AS_FALSE();
}

#endif /* BUILD_RS232 */
