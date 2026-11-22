#include "BlockMedia.h"

uint32_t BlockMedia::blockCount() const
{
    uint32_t count = 0;
    for (uint16_t cylinder = 0; cylinder < cylinderCount(); cylinder++)
        for (uint8_t side = 0; side < sideCount(); side++)
            count += trackGeometry(cylinder, side).sectorCount;
    return count;
}

uint16_t BlockMedia::blockSize(uint32_t block) const
{
    CHS chs;
    if (blockToCHS(block, chs).is_error())
        return 0;
    return trackGeometry(chs.cylinder, chs.side).sectorSize;
}

error_is_true BlockMedia::readBlock(uint32_t block, ByteBuffer &buffer)
{
    uint16_t size = blockSize(block);
    uint32_t offset;
    if (_fileh == nullptr || size == 0 || blockToOffset(block, offset).is_error())
        RETURN_ERROR_AS_TRUE();

    buffer.resize(size);
    if (seekFor(offset, false).is_error())
        RETURN_ERROR_AS_TRUE();
    if (fnio::fread(buffer.data(), 1, size, _fileh) != size)
    {
        forgetPosition();
        RETURN_ERROR_AS_TRUE();
    }
    _nextOffset = offset + size;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true BlockMedia::writeBlock(uint32_t block, const ByteBuffer &buffer)
{
    uint16_t size = blockSize(block);
    uint32_t offset;
    if (_fileh == nullptr || isReadOnly() || size == 0 || buffer.size() != size
        || blockToOffset(block, offset).is_error())
        RETURN_ERROR_AS_TRUE();

    if (seekFor(offset, true).is_error())
        RETURN_ERROR_AS_TRUE();
    if (fnio::fwrite(buffer.data(), 1, size, _fileh) != size)
    {
        forgetPosition();
        RETURN_ERROR_AS_TRUE();
    }
    _nextOffset = offset + size;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true BlockMedia::seekFor(uint32_t offset, bool writing)
{
    if (_nextOffset == offset && _lastWasWrite == writing)
        RETURN_SUCCESS_AS_FALSE();

    _lastWasWrite = writing;
    if (fnio::fseek(_fileh, offset, SEEK_SET) != 0)
    {
        forgetPosition();
        RETURN_ERROR_AS_TRUE();
    }
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true BlockMedia::blockToCHS(uint32_t block, CHS &chs) const
{
    for (uint16_t cylinder = 0; cylinder < cylinderCount(); cylinder++)
    {
        for (uint8_t side = 0; side < sideCount(); side++)
        {
            uint8_t sectors = trackGeometry(cylinder, side).sectorCount;
            if (block < sectors)
            {
                chs = {cylinder, side, static_cast<uint8_t>(block)};
                RETURN_SUCCESS_AS_FALSE();
            }
            block -= sectors;
        }
    }
    RETURN_ERROR_AS_TRUE();
}

error_is_true BlockMedia::blockToOffset(uint32_t block, uint32_t &offset) const
{
    CHS chs;
    if (blockToCHS(block, chs).is_error())
        RETURN_ERROR_AS_TRUE();
    return offsetFromCHS(chs, offset);
}
