#include "ROMMedia.h"

error_is_true ROMMedia::readSize()
{
    fnio::fseek(_fileh, 0, SEEK_END);
    long length = fnio::ftell(_fileh);
    fnio::fseek(_fileh, 0, SEEK_SET);
    if (length < 0)
        RETURN_ERROR_AS_TRUE();

    _size = length;
    RETURN_SUCCESS_AS_FALSE();
}

size_result_t ROMMedia::read(ByteBuffer &buffer, size_t length)
{
    if (_fileh == nullptr)
        RETURN_SIZE_ERROR();

    buffer.resize(length);
    size_t rlen = fnio::fread(buffer.data(), 1, buffer.size(), _fileh);
    buffer.resize(rlen);
    RETURN_SIZE(buffer.size());
}

error_is_true ROMMedia::close()
{
    if (!_fileh)
        RETURN_ERROR_AS_TRUE();
    fnio::fclose(_fileh);
    _fileh = nullptr;
    RETURN_SUCCESS_AS_FALSE();
}
