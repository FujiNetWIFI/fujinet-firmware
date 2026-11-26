#include "MSXROM.h"
#include "MSXROMDatabase.h"
#include "hash.h"
#include "string_utils.h"

#include "debug.h"

#include <algorithm>
#include <cctype>
#include <span>
#include <string_view>

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

// Fixed 16K windows, no bank register. Unless the image is placed at a page, up
// to 32K starts in page 1 and a bigger one starts in page 0 and takes all four
// windows, as the cart needs a power-of-two count.
static constexpr MapperWindow PLAIN_WINDOWS_PAGE1[] = {
    {0, 0, 0x4000, 0x8000},
    {0, 0, 0x8000, 0xC000},
};
static constexpr MapperWindow PLAIN_WINDOWS_PAGE2[] = {
    {0, 0, 0x8000, 0xC000},
    {0, 0, 0xC000, 0x10000},
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
// "Game [ASCII8].rom"; Unknown if there is none or it isn't one we know.
static MSXROM::Mapper mapper_from_name(const std::string &path)
{
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot == 0 || path[dot - 1] != ']')
        return MSXROM::Mapper::Unknown;
    size_t open = path.rfind('[', dot - 1);
    if (open == std::string::npos)
        return MSXROM::Mapper::Unknown;

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
    return MSXROM::Mapper::Unknown;
}

struct DBType
{
    std::string_view name;
    MSXROM::Mapper mapper;
};

static constexpr DBType DB_TYPES[] = {
    {"ASCII8", MSXROM::Mapper::Ascii8},
    {"ASCII16", MSXROM::Mapper::Ascii16},
    {"Konami", MSXROM::Mapper::Konami},
    {"KonamiSCC", MSXROM::Mapper::KonamiScc},
    {"Page2", MSXROM::Mapper::PlainPage2},
    {"0x8000", MSXROM::Mapper::PlainPage2},
    {"0x0000", MSXROM::Mapper::PlainPage0},
};

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
    _hashed = 0;
    _sha1.reset();
    if (_mapper == Mapper::Unknown)
        _sha1.emplace(Hash::Algorithm::SHA1);
    RETURN_SUCCESS_AS_FALSE();
}

static MapperLayout make_layout(std::span<const MapperWindow> windows,
                                std::span<const MapperMirror> mirrors = {})
{
    return MapperLayout{{windows.begin(), windows.end()}, {mirrors.begin(), mirrors.end()}};
}

size_result_t MSXROM::read(ByteBuffer &buffer, size_t length)
{
    auto got = ROMMedia::read(buffer, length);
    if (got.is_success() && !buffer.empty() && _sha1
        && _sha1->update(buffer.data(), buffer.size()).is_success())
        _hashed += buffer.size();
    return got;
}

// Only if the whole image was hashed.
void MSXROM::lookupDatabase()
{
    if (!_sha1 || _size == 0 || _hashed != _size)
        return;

    uint8_t digest[20];
    auto finished = _sha1->finish(digest, sizeof(digest));
    _sha1.reset();
    if (finished.is_error())
        return;
    std::string hash = mstr::toHex(digest, sizeof(digest));
    FILE *db = msx_rom_db_open();
    if (db == nullptr)
    {
        Debug_printf("MSX ROM %s: no database\n", hash.c_str());
        return;
    }
    auto type = msx_rom_db_lookup(db, hash);
    fclose(db);
    if (!type)
    {
        Debug_printf("MSX ROM %s: not in database\n", hash.c_str());
        return;
    }

    Debug_printf("MSX ROM %s: database type %s\n", hash.c_str(), type->c_str());
    for (const DBType &entry : DB_TYPES)
    {
        if (entry.name == *type)
        {
            _mapper = entry.mapper;
            return;
        }
    }
}

std::optional<MapperLayout> MSXROM::mapperLayout()
{
    if (_mapper == Mapper::Unknown)
    {
        lookupDatabase();
        if (_mapper == Mapper::Unknown)
            _mapper = Mapper::Plain;
    }

    size_t pages = (_size + MSX_PAGE - 1) / MSX_PAGE;
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
    case Mapper::PlainPage0:
        return make_layout(std::span(PLAIN_WINDOWS_PAGE0).first(pages <= 2 ? pages : 4));
    case Mapper::PlainPage2:
        return make_layout(std::span(PLAIN_WINDOWS_PAGE2).first(std::min<size_t>(pages, 2)));
    case Mapper::Unknown:
    case Mapper::Plain:
        break;
    }

    if (pages <= 2)
        return make_layout(std::span(PLAIN_WINDOWS_PAGE1).first(pages));
    // Over 64K only the first 64K is reachable
    return make_layout(PLAIN_WINDOWS_PAGE0);
}
