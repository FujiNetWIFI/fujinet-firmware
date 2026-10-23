#ifndef IMD_FNIO_H
#define IMD_FNIO_H

#include "imd/IMDImage.h"

#include "fnio.h"

// IMDImage<fnFile>'s file operations, so it works the same on every host
// FujiNet mounts from.
inline IMDStatus imd_read_at(fnFile &file, uint32_t off, void *dst, uint32_t len)
{
    if (fnio::fseek(&file, (long)off, SEEK_SET) != 0 || fnio::fread(dst, 1, len, &file) != len)
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

inline IMDStatus imd_write_at(fnFile &file, uint32_t off, const void *src, uint32_t len)
{
    if (fnio::fseek(&file, (long)off, SEEK_SET) != 0 || fnio::fwrite(src, 1, len, &file) != len)
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

inline uint32_t imd_size(fnFile &file)
{
    if (fnio::fseek(&file, 0, SEEK_END) != 0)
        return 0;
    long end = fnio::ftell(&file);
    return end < 0 ? 0 : (uint32_t)end;
}

inline IMDStatus imd_flush(fnFile &file)
{
    fnio::fflush(&file);
    return IMDStatus::Ok;
}

#endif // IMD_FNIO_H
