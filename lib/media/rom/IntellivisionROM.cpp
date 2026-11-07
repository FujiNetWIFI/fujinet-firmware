#include "IntellivisionROM.h"

// A .cfg is a few kilobytes at most, so a bigger file isn't one.
constexpr size_t MAPPER_FILE_MAX = 16_kB;

// Without a .cfg the cart maps a flat image only at the sizes the original
// library came in, and refuses any other.
constexpr size_t INTV_DEFAULT_MAP_SIZES[] = {
    4_kB, 8_kB, 12_kB, 16_kB,
    24_kB, 32_kB, 40_kB, 48_kB,
};

static bool has_default_map(size_t size)
{
    for (size_t mapSize : INTV_DEFAULT_MAP_SIZES)
        if (size == mapSize)
            return true;
    return false;
}

// Any .int or .itv, or a .bin or .rom with a same-named .cfg mapper file
// beside it. The image is 16-bit words, so its size is even; without a .cfg it
// must also be a size the cart has a default map for. Without a host no .cfg
// is looked for, so only .int and .itv are taken.
error_is_true IntellivisionROM::attach()
{
    std::optional<std::string> extension =
        matchExtension(_path, {".int", ".itv", ".bin", ".rom"});
    if (!extension.has_value())
        RETURN_ERROR_AS_TRUE();

    // Other consoles use .bin and .rom too, so only a .cfg makes one Intellivision
    bool needsMapper = *extension == ".bin" || *extension == ".rom";

    std::optional<std::string> mapperPath = findSibling(_host, _path, ".cfg");
    if (!mapperPath.has_value() && needsMapper)
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size == 0 || _size % 2 != 0)
        RETURN_ERROR_AS_TRUE();

    if (!mapperPath.has_value())
    {
        if (!has_default_map(_size))
            RETURN_ERROR_AS_TRUE();
        RETURN_SUCCESS_AS_FALSE();
    }

    _mapperInfo = contentsOfHostFile(_host, *mapperPath, MAPPER_FILE_MAX);
    if (!_mapperInfo.has_value())
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}

std::optional<std::pmr::vector<uint8_t>> IntellivisionROM::mapperInfo()
{
    if (!_mapperInfo.has_value())
        return std::nullopt;
    // Copy with the same allocator, so the copy stays in PSRAM too
    return std::pmr::vector<uint8_t>(*_mapperInfo, _mapperInfo->get_allocator());
}
