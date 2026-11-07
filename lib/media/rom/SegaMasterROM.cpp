#include "SegaMasterROM.h"

// A .cfg is a few kilobytes at most, so a bigger file isn't one.
constexpr size_t MAPPER_FILE_MAX = 16_kB;

constexpr size_t COPIER_HEADER_SIZE = 512;
constexpr size_t SMS_PAGE = 16_kB;

// Not every image carries the "TMR SEGA" header, so it can't be required. A
// copier's 512-byte header in front of whole 16K pages is allowed, but there
// must be ROM data after it. A ROM that fits in one 16K page may be any size,
// since the cart pads it out; a bigger one is banked, so it must be whole pages.
error_is_true SegaMasterROM::attach()
{
    if (!matchExtension(_path, {".sms"}).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size == 0)
        RETURN_ERROR_AS_TRUE();

    size_t romSize = _size;
    if (romSize % SMS_PAGE == COPIER_HEADER_SIZE)
        romSize -= COPIER_HEADER_SIZE;

    if (romSize == 0 || (romSize > SMS_PAGE && romSize % SMS_PAGE != 0))
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

std::optional<std::pmr::vector<uint8_t>> SegaMasterROM::mapperInfo()
{
    if (!_mapperInfo.has_value())
        return std::nullopt;
    // Copy with the same allocator, so the copy stays in PSRAM too
    return std::pmr::vector<uint8_t>(*_mapperInfo, _mapperInfo->get_allocator());
}
