#include "MSXROM.h"

#include <cctype>
#include <span>

constexpr size_t MSX_PAGE = 16_kB;

static constexpr MapperWindow ASCII8_WINDOWS[] = {
    {0x6000, 0x6800, 0x4000, 0x6000},
    {0x6800, 0x7000, 0x6000, 0x8000},
    {0x7000, 0x7800, 0x8000, 0xA000},
    {0x7800, 0x8000, 0xA000, 0xC000},
};

static constexpr MapperWindow ASCII16_WINDOWS[] = {
    {0x6000, 0x6800, 0x4000, 0x8000},
    {0x7000, 0x7800, 0x8000, 0xC000},
};

static constexpr MapperWindow KONAMI_WINDOWS[] = {
    {0x0000, 0x0000, 0x4000, 0x6000},
    {0x6000, 0x8000, 0x6000, 0x8000},
    {0x8000, 0xA000, 0x8000, 0xA000},
    {0xA000, 0xC000, 0xA000, 0xC000},
};
static constexpr MapperMirror KONAMI_MIRRORS[] = {
    {0x0000, 0x4000, 0x4000},
    {0xC000, 0x10000, 0x8000},
};

static constexpr MapperWindow KONAMI_SCC_WINDOWS[] = {
    {0x5000, 0x5800, 0x4000, 0x6000},
    {0x7000, 0x7800, 0x6000, 0x8000},
    {0x9000, 0x9800, 0x8000, 0xA000},
    {0xB000, 0xB800, 0xA000, 0xC000},
};
static constexpr MapperMirror KONAMI_SCC_MIRRORS[] = {
    {0x0000, 0x4000, 0x8000},
    {0xC000, 0x10000, 0x4000},
};

// Fixed 16K windows, no bank register. Up to 32K starts in page 1; a bigger
// image starts in page 0 and takes all four windows, as the cart needs a
// power-of-two count.
static constexpr MapperWindow PLAIN_WINDOWS_PAGE1[] = {
    {0, 0, 0x4000, 0x8000},
    {0, 0, 0x8000, 0xC000},
};
static constexpr MapperWindow PLAIN_WINDOWS_PAGE0[] = {
    {0, 0, 0x0000, 0x4000},
    {0, 0, 0x4000, 0x8000},
    {0, 0, 0x8000, 0xC000},
    {0, 0, 0xC000, 0x10000},
};

static bool has_cart_id(const uint8_t header[2])
{
    return header[0] == 'A' && header[1] == 'B';
}

// The tag in square brackets just before the extension, such as
// "Game [ASCII8].rom"; Plain if there is none or it isn't one we know.
static MSXROM::Mapper mapper_from_name(const std::string &path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot == 0 || path[dot - 1] != ']')
        return MSXROM::Mapper::Plain;
    size_t open = path.rfind('[', dot - 1);
    if (open == std::string::npos)
        return MSXROM::Mapper::Plain;

    std::string tag = path.substr(open + 1, dot - open - 2);
    for (char &ch : tag)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (tag == "ascii8")
        return MSXROM::Mapper::Ascii8;
    if (tag == "ascii16")
        return MSXROM::Mapper::Ascii16;
    if (tag == "konami")
        return MSXROM::Mapper::Konami;
    if (tag == "konamiscc")
        return MSXROM::Mapper::KonamiScc;
    return MSXROM::Mapper::Plain;
}

// An MSX cartridge starts with "AB". A plain image over 32K that starts in
// page 0 has it at $4000 instead.
error_is_true MSXROM::attach()
{
    // .rom is shared with other consoles; the cart ID is what makes one MSX's.
    if (!matchExtension(_path, extensions).has_value())
        RETURN_ERROR_AS_TRUE();

    if (readSize().is_error() || _size < 2)
        RETURN_ERROR_AS_TRUE();

    uint8_t header[2];
    bool found = fnio::fread(header, 1, sizeof(header), _fileh) == sizeof(header)
        && has_cart_id(header);
    if (!found && _size > 2 * MSX_PAGE && fnio::fseek(_fileh, MSX_PAGE, SEEK_SET) == 0
        && fnio::fread(header, 1, sizeof(header), _fileh) == sizeof(header))
        found = has_cart_id(header);

    fnio::fseek(_fileh, 0, SEEK_SET);
    if (!found)
        RETURN_ERROR_AS_TRUE();

    _mapper = mapper_from_name(_path);
    RETURN_SUCCESS_AS_FALSE();
}

static MapperLayout make_layout(std::span<const MapperWindow> windows,
                                std::span<const MapperMirror> mirrors = {})
{
    return MapperLayout{{windows.begin(), windows.end()}, {mirrors.begin(), mirrors.end()}};
}

std::optional<MapperLayout> MSXROM::mapperLayout()
{
    switch (_mapper)
    {
    case Mapper::Ascii8:
        return make_layout(ASCII8_WINDOWS);
    case Mapper::Ascii16:
        return make_layout(ASCII16_WINDOWS);
    case Mapper::Konami:
        return make_layout(KONAMI_WINDOWS, KONAMI_MIRRORS);
    case Mapper::KonamiScc:
        return make_layout(KONAMI_SCC_WINDOWS, KONAMI_SCC_MIRRORS);
    case Mapper::Plain:
        break;
    }

    size_t pages = (_size + MSX_PAGE - 1) / MSX_PAGE;
    if (pages <= 2)
        return make_layout(std::span(PLAIN_WINDOWS_PAGE1).first(pages));
    // Over 64K only the first 64K is reachable
    return make_layout(PLAIN_WINDOWS_PAGE0);
}
