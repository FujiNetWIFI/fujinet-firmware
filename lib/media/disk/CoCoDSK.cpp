#include "CoCoDSK.h"

#include "fujiHost.h"

#include <algorithm>
#include <cstring>

// The FNHS sector that lists the sectors a game keeps its high scores in.
struct HighScoreMarker
{
    char magic[4];
    uint8_t version;
    uint8_t rangeCount;
    struct
    {
        u16be_t start;
        u16be_t count;
    } __attribute__((packed)) ranges[COCO_DSK_HIGH_SCORE_RANGES];
} __attribute__((packed));

uint16_t CoCoDSK::cylinderCount() const
{
    return (blockCount() + COCO_DSK_SECTORS_TRACK - 1) / COCO_DSK_SECTORS_TRACK;
}

uint16_t CoCoDSK::blockSize(uint32_t block) const
{
    return block < blockCount() ? COCO_DSK_SECTOR_SIZE : 0;
}

error_is_true CoCoDSK::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (block >= blockCount())
        RETURN_ERROR_AS_TRUE();
    offset = block * COCO_DSK_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true CoCoDSK::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();
    if (readSize().is_error() || blockCount() == 0)
        RETURN_ERROR_AS_TRUE();

    readHighScoreMarker();
    RETURN_SUCCESS_AS_FALSE();
}

void CoCoDSK::readHighScoreMarker()
{
    _highScoreRangeCount = 0;

    ByteBuffer sector;
    if (readBlock(COCO_DSK_HIGH_SCORE_BLOCK, sector).is_error())
        return;

    HighScoreMarker marker;
    memcpy(&marker, sector.data(), sizeof(marker));
    if (memcmp(marker.magic, "FNHS", 4) != 0 || marker.version != 1)
        return;

    _highScoreRangeCount = std::min(marker.rangeCount, COCO_DSK_HIGH_SCORE_RANGES);
    for (uint8_t idx = 0; idx < _highScoreRangeCount; idx++)
        _highScoreRanges[idx] = {marker.ranges[idx].start, marker.ranges[idx].count};
}

bool CoCoDSK::isHighScoreBlock(uint32_t block) const
{
    for (uint8_t idx = 0; idx < _highScoreRangeCount; idx++)
        if (block >= _highScoreRanges[idx].start
            && block < (uint32_t)_highScoreRanges[idx].start + _highScoreRanges[idx].count)
            return true;
    return false;
}

error_is_true CoCoDSK::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    if (!_readOnly)
        return BlockMedia::writeBlock(block, buffer);

    uint32_t offset;
    if (_host == nullptr || !isHighScoreBlock(block) || buffer.size() != blockSize(block)
        || blockToOffset(block, offset).is_error())
        RETURN_ERROR_AS_TRUE();

    fnFile *writable = _host->fnfile_open(_path.c_str(), nullptr, 0, "rb+");
    if (writable == nullptr)
        RETURN_ERROR_AS_TRUE();

    error_is_true result(fnio::fseek(writable, offset, SEEK_SET) != 0
                         || fnio::fwrite(buffer.data(), 1, buffer.size(), writable)
                             != buffer.size());
    fnio::fclose(writable);

    // The write went through another handle, so this one's cache and position are stale.
    fnio::finvalidate_cache(_fileh);
    forgetPosition();
    return result;
}
