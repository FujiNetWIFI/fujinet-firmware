#include "Atari7800ROM.h"

#include <cstring>

// A .cfg is a few kilobytes at most, so a bigger file isn't one.
constexpr size_t MAPPER_FILE_MAX = 16_kB;

constexpr size_t A78_HEADER_SIZE = 128;
constexpr size_t A78_BANK_SIZE = 16_kB;

// An image with the 128-byte .a78 header must have ROM data after it. One
// without is taken only in whole 16K banks, the unit every cart and SuperGame
// bank comes in, since nothing else says it's a 7800 image.
error_is_true Atari7800ROM::attach()
{
    if (!matchExtension(_path, {".a78"}).has_value())
        RETURN_ERROR_AS_TRUE();

    char signature[10];
    size_t got = fnio::fread(signature, 1, sizeof(signature), _fileh);
    bool hasHeader = got == sizeof(signature) && memcmp(signature + 1, "ATARI7800", 9) == 0;

    if (readSize().is_error() || _size == 0)
        RETURN_ERROR_AS_TRUE();

    if (hasHeader && _size <= A78_HEADER_SIZE)
        RETURN_ERROR_AS_TRUE();

    if (!hasHeader && _size % A78_BANK_SIZE != 0)
        RETURN_ERROR_AS_TRUE();

    // The cart takes a mapper= override from a .cfg beside the image, which is
    // passed through unread.
    std::optional<std::string> mapperPath = findSibling(_host, _path, ".cfg");
    if (mapperPath.has_value())
    {
        _mapperInfo = contentsOfHostFile(_host, *mapperPath, MAPPER_FILE_MAX);
        if (!_mapperInfo.has_value())
            RETURN_ERROR_AS_TRUE();
    }

    RETURN_SUCCESS_AS_FALSE();
}

std::optional<std::pmr::vector<uint8_t>> Atari7800ROM::mapperInfo()
{
    if (!_mapperInfo.has_value())
        return std::nullopt;
    // Copy with the same allocator, so the copy stays in PSRAM too
    return std::pmr::vector<uint8_t>(*_mapperInfo, _mapperInfo->get_allocator());
}
