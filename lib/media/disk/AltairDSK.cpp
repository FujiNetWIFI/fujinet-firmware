#include "AltairDSK.h"

struct AltairGeometry
{
    size_t size;
    uint16_t cylinders;
    uint8_t sectorsPerTrack;
};

// The larger 8" and minidisk sizes carry the padding left by the tools that
// made most published images.
static constexpr AltairGeometry ALTAIR_GEOMETRIES[] = {
    {337568, 77, 32},    // Altair 8"
    {337664, 77, 32},
    {76720, 35, 16},     // Altair minidisk
    {76800, 35, 16},
    {8978432, 2048, 32}, // FDC+ 8 MB serial drive
};

TrackGeometry AltairDSK::trackGeometry(uint16_t cylinder, uint8_t side) const
{
    if (cylinder >= _cylinders || side > 0)
        return {0, 0};
    return {_sectorsPerTrack, ALTAIR_DSK_SECTOR_SIZE, TrackEncoding::FM};
}

uint16_t AltairDSK::blockSize(uint32_t block) const
{
    return block < blockCount() ? ALTAIR_DSK_SECTOR_SIZE : 0;
}

error_is_true AltairDSK::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (block >= blockCount())
        RETURN_ERROR_AS_TRUE();
    offset = block * ALTAIR_DSK_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true AltairDSK::attach()
{
    if (!matchExtension(_path, extensions).has_value() || readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    for (const AltairGeometry &geometry : ALTAIR_GEOMETRIES)
    {
        if (geometry.size == _size)
        {
            _cylinders = geometry.cylinders;
            _sectorsPerTrack = geometry.sectorsPerTrack;
            RETURN_SUCCESS_AS_FALSE();
        }
    }
    RETURN_ERROR_AS_TRUE();
}

error_is_true AltairDSK::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    if (BlockMedia::writeBlock(block, buffer).is_error())
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(fnio::fflush(_fileh) != 0);
}
