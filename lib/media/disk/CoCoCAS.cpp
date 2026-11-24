#include "CoCoCAS.h"

#include <algorithm>
#include <string>

constexpr size_t DECB_VOLUME_NAME_LENGTH = 8;

TrackGeometry CoCoCAS::trackGeometry(uint16_t cylinder, uint8_t side) const
{
    if (cylinder >= cylinderCount() || side >= sideCount())
        return {0, 0};
    return {DECB_SECTORS_TRACK, DECB_SECTOR_SIZE};
}

error_is_true CoCoCAS::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();
    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    _source.attach(_fileh, _size);
    if (!_index.build(&_reader))
        RETURN_ERROR_AS_TRUE();

    // The disk is named after the image file.
    size_t slash = _path.find_last_of('/');
    std::string volume = _path.substr(slash == std::string::npos ? 0 : slash + 1);
    volume = volume.substr(0, std::min(volume.find('.'), DECB_VOLUME_NAME_LENGTH));
    RETURN_ERROR_IF(!_layout.build(&_index, volume.c_str()));
}

error_is_true CoCoCAS::readBlock(uint32_t block, ByteBuffer &buffer)
{
    uint16_t size = blockSize(block);
    if (size == 0)
        RETURN_ERROR_AS_TRUE();

    buffer.resize(size);
    RETURN_ERROR_IF(!_layout.read_sector(block, buffer.data()));
}
