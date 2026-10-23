// IMD (ImageDisk) disk image reader/writer. See IMDImage.h.

#include "IMDImage.h"

#include <cctype>
#include <cstring>

IMDStatus imd_read_at(std::fstream &file, uint32_t off, void *dst, uint32_t len)
{
    file.clear();
    if (!file.seekg(off) || !file.read(static_cast<char *>(dst), len))
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

IMDStatus imd_write_at(std::fstream &file, uint32_t off, const void *src, uint32_t len)
{
    file.clear();
    if (!file.seekp(off) || !file.write(static_cast<const char *>(src), len))
        return IMDStatus::IoError;
    return IMDStatus::Ok;
}

uint32_t imd_size(std::fstream &file)
{
    file.clear();
    std::streampos end = file.seekg(0, std::ios::end).tellg();
    return end < 0 ? 0 : (uint32_t)end;
}

IMDStatus imd_flush(std::fstream &file)
{
    return file.flush() ? IMDStatus::Ok : IMDStatus::IoError;
}

const char *imd_status_str(IMDStatus s)
{
    switch (s)
    {
    case IMDStatus::Ok:             return "Ok";
    case IMDStatus::NotIMD:         return "NotIMD";
    case IMDStatus::Truncated:      return "Truncated";
    case IMDStatus::BadRecordType:  return "BadRecordType";
    case IMDStatus::BadSizeCode:    return "BadSizeCode";
    case IMDStatus::NoSuchSector:   return "NoSuchSector";
    case IMDStatus::Unavailable:    return "Unavailable";
    case IMDStatus::BufferTooSmall: return "BufferTooSmall";
    case IMDStatus::WriteRefused:   return "WriteRefused";
    case IMDStatus::ReadOnly:       return "ReadOnly";
    case IMDStatus::IoError:        return "IoError";
    case IMDStatus::TooLarge:       return "TooLarge";
    }
    return "?";
}

uint16_t imd_mode_rate_kbps(uint8_t mode)
{
    switch (mode)
    {
    case 0: case 3: return 500;
    case 1: case 4: return 300;
    case 2: case 5: return 250;
    case 6: case 9: return 1000; // libdsk extension
    default:        return 0;
    }
}

bool imd_mode_is_mfm(uint8_t mode)
{
    return (mode >= 3 && mode <= 5) || mode == 9;
}

bool imd_mode_is_valid(uint8_t mode)
{
    return mode <= 6 || mode == 9;
}

bool imd_looks_like_extension(const char *filename)
{
    if (filename == nullptr)
        return false;
    size_t l = strlen(filename);
    if (l <= 4 || filename[l - 4] != '.')
        return false;
    for (size_t i = 0; i < 3; i++)
        if (std::toupper((unsigned char)filename[l - 3 + i]) != "IMD"[i])
            return false;
    return true;
}
