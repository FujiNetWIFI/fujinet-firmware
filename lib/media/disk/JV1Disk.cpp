#include "JV1Disk.h"

// The names attach() takes.
static constexpr const char *JV1_OPEN_EXTENSIONS[] = {".jv1", ".dsk"};

// The track counts a .dsk is taken at: Model I 35- and 40-track drives, and 80.
static constexpr uint16_t JV1_DSK_CYLINDERS[] = {35, 40, 80};

TrackGeometry JV1Disk::trackGeometry(uint16_t cylinder, uint8_t side) const
{
    if (cylinder >= _cylinders || side > 0)
        return {0, 0};
    return {JV1_SECTORS_TRACK, JV1_SECTOR_SIZE, TrackEncoding::FM};
}

uint16_t JV1Disk::blockSize(uint32_t block) const
{
    return block < blockCount() ? JV1_SECTOR_SIZE : 0;
}

error_is_true JV1Disk::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (block >= blockCount())
        RETURN_ERROR_AS_TRUE();
    offset = block * JV1_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true JV1Disk::attach()
{
    std::optional<std::string> extension = matchExtension(_path, JV1_OPEN_EXTENSIONS);
    if (!extension.has_value() || readSize().is_error() || _size == 0 || _size % JV1_TRACK_SIZE)
        RETURN_ERROR_AS_TRUE();

    size_t cylinders = _size / JV1_TRACK_SIZE;
    if (cylinders > JV1_MAX_CYLINDERS)
        RETURN_ERROR_AS_TRUE();
    if (*extension == ".dsk")
    {
        bool known = false;
        for (uint16_t count : JV1_DSK_CYLINDERS)
            known = known || count == cylinders;
        if (!known)
            RETURN_ERROR_AS_TRUE();
    }
    _cylinders = (uint16_t)cylinders;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true JV1Disk::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    if (BlockMedia::writeBlock(block, buffer).is_error())
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(fnio::fflush(_fileh) != 0);
}
