#include "ColecoROM.h"

// A .cfg is a few kilobytes at most, so a bigger file isn't one.
constexpr size_t MAPPER_FILE_MAX = 16_kB;

constexpr size_t COLECO_WINDOW = 32_kB;
constexpr size_t COLECO_BANK = 16_kB;

static bool is_cart_header(const uint8_t header[2])
{
    return (header[0] == 0xAA && header[1] == 0x55)
        || (header[0] == 0x55 && header[1] == 0xAA);
}

// The BIOS starts a cartridge only if it finds AA 55 or 55 AA at $8000. That's
// the start of the image, or for a banked cart such as a MegaCart the start of
// its last 16K bank, which is what the console sees at power-up.
error_is_true ColecoROM::attach()
{
    if (!matchExtension(_path, {".col"}).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size < 2)
        RETURN_ERROR_AS_TRUE();

    uint8_t header[2];
    if (fnio::fread(header, 1, sizeof(header), _fileh) != sizeof(header))
        RETURN_ERROR_AS_TRUE();

    bool found = is_cart_header(header);
    if (!found && _size > COLECO_WINDOW && _size % COLECO_BANK == 0
        && fnio::fseek(_fileh, _size - COLECO_BANK, SEEK_SET) == 0
        && fnio::fread(header, 1, sizeof(header), _fileh) == sizeof(header))
        found = is_cart_header(header);

    fnio::fseek(_fileh, 0, SEEK_SET);
    if (!found)
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

std::optional<std::pmr::vector<uint8_t>> ColecoROM::mapperInfo()
{
    if (!_mapperInfo.has_value())
        return std::nullopt;
    // Copy with the same allocator, so the copy stays in PSRAM too
    return std::pmr::vector<uint8_t>(*_mapperInfo, _mapperInfo->get_allocator());
}
