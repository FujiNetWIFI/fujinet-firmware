#include "DiskMedia.h"

error_is_true DiskMedia::close()
{
    if (!_fileh)
        RETURN_ERROR_AS_TRUE();
    fnio::fclose(_fileh);
    _fileh = nullptr;
    RETURN_SUCCESS_AS_FALSE();
}

error_is_true DiskMedia::readSize()
{
    fnio::fseek(_fileh, 0, SEEK_END);
    long length = fnio::ftell(_fileh);
    fnio::fseek(_fileh, 0, SEEK_SET);
    if (length < 0)
        RETURN_ERROR_AS_TRUE();

    _size = length;
    RETURN_SUCCESS_AS_FALSE();
}
