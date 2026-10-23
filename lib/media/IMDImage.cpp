// IMD (ImageDisk) helpers that don't depend on the file type. See IMDImage.h.

#include "IMDImage.h"

#include <cctype>
#include <cstring>

const char *imd_status_str(IMDStatus status)
{
    switch (status)
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
    size_t len = strlen(filename);
    if (len <= 4 || filename[len - 4] != '.')
        return false;
    for (size_t idx = 0; idx < 3; idx++)
        if (std::toupper((unsigned char)filename[len - 3 + idx]) != "IMD"[idx])
            return false;
    return true;
}
