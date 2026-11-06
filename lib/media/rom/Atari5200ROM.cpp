#include "Atari5200ROM.h"

#include <cstring>

// A .cfg is a few kilobytes at most, so a bigger file isn't one.
constexpr size_t MAPPER_FILE_MAX = 16_kB;

constexpr size_t CAR_HEADER_SIZE = 16;
constexpr size_t A52_SIZE_UNIT = 4_kB;

// A 16-byte .car header in front of whole 4K units is optional, but an image
// that has one must have ROM data after it. Which sizes it can map is left to
// the cart.
error_is_true Atari5200ROM::attach()
{
    if (!matchExtension(_path, {".a52"}).has_value())
        RETURN_ERROR_AS_TRUE();

    char signature[4];
    size_t got = fnio::fread(signature, 1, sizeof(signature), _fileh);

    if (readSize().is_error() || _size == 0)
        RETURN_ERROR_AS_TRUE();

    bool hasHeader = got == sizeof(signature) && memcmp(signature, "CART", 4) == 0
        && _size % A52_SIZE_UNIT == CAR_HEADER_SIZE;
    if (hasHeader && _size <= CAR_HEADER_SIZE)
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

std::optional<std::pmr::vector<uint8_t>> Atari5200ROM::mapperInfo()
{
    if (!_mapperInfo.has_value())
        return std::nullopt;
    // Copy with the same allocator, so the copy stays in PSRAM too
    return std::pmr::vector<uint8_t>(*_mapperInfo, _mapperInfo->get_allocator());
}
