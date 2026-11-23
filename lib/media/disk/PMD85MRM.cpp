#include "PMD85MRM.h"

#include <algorithm>

uint32_t PMD85MRM::blockCount() const
{
    return (_size + PMD85_MRM_BLOCK_SIZE - 1) / PMD85_MRM_BLOCK_SIZE;
}

uint16_t PMD85MRM::blockSize(uint32_t block) const
{
    return block < blockCount() ? PMD85_MRM_BLOCK_SIZE : 0;
}

error_is_true PMD85MRM::blockToOffset(uint32_t block, uint32_t &offset) const
{
    if (block >= blockCount())
        RETURN_ERROR_AS_TRUE();
    offset = block * PMD85_MRM_BLOCK_SIZE;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true PMD85MRM::attach()
{
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();
    if (readSize().is_error())
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(_size == 0);
}

// The last block is short in the file, so this reads what's stored and pads the rest.
error_is_true PMD85MRM::readBlock(uint32_t block, ByteBuffer &buffer)
{
    uint32_t offset;
    if (_fileh == nullptr || blockToOffset(block, offset).is_error())
        RETURN_ERROR_AS_TRUE();

    size_t stored = std::min<size_t>(PMD85_MRM_BLOCK_SIZE, _size - offset);
    buffer.assign(PMD85_MRM_BLOCK_SIZE, 0xFF);

    // The base class's record of the handle's position doesn't see this read.
    forgetPosition();
    if (fnio::fseek(_fileh, offset, SEEK_SET) != 0)
        RETURN_ERROR_AS_TRUE();
    RETURN_ERROR_IF(fnio::fread(buffer.data(), 1, stored, _fileh) != stored);
}
