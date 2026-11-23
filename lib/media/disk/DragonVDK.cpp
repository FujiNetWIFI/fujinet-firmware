#include "DragonVDK.h"

// The fixed part of the header; headerSize also counts the disk name and any
// padding after it.
struct VDKHeader
{
    char magic[2];
    u16le_t headerSize;
    uint8_t version;
    uint8_t compatVersion;
    uint8_t sourceID;
    uint8_t sourceVersion;
    uint8_t tracks;
    uint8_t sides;
    uint8_t flags;
    uint8_t compressionAndNameLength;
} __attribute__((packed));
static_assert(sizeof(VDKHeader) == 12, "VDK fixed header must be 12 bytes");

// We don't decompress.
constexpr uint8_t VDK_COMPRESSION_MASK = 0x07;

uint16_t DragonVDK::blockSize(uint32_t block) const
{
    return block >= 1 && block <= _sectors ? DRAGON_VDK_SECTOR_SIZE : 0;
}

error_is_true DragonVDK::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (blockSize(block) == 0)
        RETURN_ERROR_AS_TRUE();
    offset = _headerSize + (block - 1) * DRAGON_VDK_SECTOR_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true DragonVDK::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();
    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();

    VDKHeader header;
    if (fnio::fread(&header, 1, sizeof(header), _fileh) != sizeof(header))
        RETURN_ERROR_AS_TRUE();
    if (header.magic[0] != 'd' || header.magic[1] != 'k'
        || (header.compressionAndNameLength & VDK_COMPRESSION_MASK) != 0)
        RETURN_ERROR_AS_TRUE();

    _headerSize = header.headerSize;
    if (_headerSize < sizeof(header) || _headerSize > _size)
        RETURN_ERROR_AS_TRUE();

    _sectors = (_size - _headerSize) / DRAGON_VDK_SECTOR_SIZE;
    _tracks = header.tracks;
    _sides = header.sides;
    RETURN_ERROR_IF(_sectors == 0);
}
