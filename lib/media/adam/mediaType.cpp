#ifdef BUILD_ADAM

#include "mediaType.h"

#include <cstdint>
#include <cstring>


MediaType::~MediaType()
{
    unmount();
}

error_is_true MediaType::format(uint16_t *responsesize)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaType::read(uint32_t blockNum, uint16_t *readcount)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaType::write(uint32_t blockNum, bool verify)
{
    RETURN_ERROR_AS_TRUE();
}

error_is_true MediaType::_write_block_at(fnFile *f, uint32_t blockNum, const uint8_t *data)
{
    RETURN_ERROR_AS_TRUE();
}

namespace
{

constexpr uint32_t EOS_DIR_BLOCKS = 3;
constexpr uint32_t EOS_DIR_ENTRIES_PER_BLOCK = 39;
constexpr uint32_t EOS_MAX_BLOCKS = 0xFFFF; // EOS sizes a volume in 16 bits
constexpr uint8_t EOS_NAME_END = 0x03;

enum class EosAttr : uint8_t
{
    BLOCKS_LEFT = 0x01,
    VOLUME = 0x80 | EOS_DIR_BLOCKS,
    BOOT = 0x88,
    DIRECTORY = 0xC8,
};

struct EosDirEntry
{
    uint8_t name[12];
    EosAttr attr;
    u32le_t start;
    u16le_t allocated;
    u16le_t used;
    u16le_t lastBytes;
    uint8_t year;
    uint8_t month;
    uint8_t day;
} __attribute__((packed));
static_assert(sizeof(EosDirEntry) == 26, "EOS directory entries are 26 bytes");

// Fills the name field: name, end marker, then spaces
void eos_set_name(EosDirEntry &entry, const char *name)
{
    size_t len = strlen(name);

    memset(entry.name, ' ', sizeof(entry.name));
    memcpy(entry.name, name, len);
    entry.name[len] = EOS_NAME_END;
}

} // namespace

success_is_true MediaType::create(fnFile *f, uint32_t numBlocks)
{
    uint8_t block[MEDIA_BLOCK_SIZE] = {};

    for (uint32_t idx = 0; idx < numBlocks; idx++)
    {
        if (fnio::fwrite(block, 1, sizeof(block), f) != sizeof(block))
            RETURN_ERROR_AS_FALSE();
    }

    // Too small or too big for a directory; leave it blank
    if (numBlocks < EOS_DIR_BLOCKS + 1 || numBlocks > EOS_MAX_BLOCKS)
        RETURN_SUCCESS_AS_TRUE();

    // Boot block: DI, JP SmartWriter
    const uint8_t boot[] = {0xF3, 0xC3, 0xE7, 0xFC};
    memcpy(block, boot, sizeof(boot));
    if (_write_block_at(f, 0, block).is_error())
        RETURN_ERROR_AS_FALSE();

    EosDirEntry entries[EOS_DIR_BLOCKS * EOS_DIR_ENTRIES_PER_BLOCK] = {};

    EosDirEntry &volume = entries[0];
    eos_set_name(volume, "");
    volume.attr = EosAttr::VOLUME;
    volume.start = 0xFF00AA55; // check code 55 AA 00 FF
    volume.allocated = numBlocks;

    EosDirEntry &bootEntry = entries[1];
    eos_set_name(bootEntry, "BOOT");
    bootEntry.attr = EosAttr::BOOT;
    bootEntry.start = 0;
    bootEntry.allocated = 1;
    bootEntry.used = 1;

    EosDirEntry &directory = entries[2];
    eos_set_name(directory, "DIRECTORY");
    directory.attr = EosAttr::DIRECTORY;
    directory.start = 1;
    directory.allocated = EOS_DIR_BLOCKS;
    directory.used = EOS_DIR_BLOCKS;
    directory.lastBytes = MEDIA_BLOCK_SIZE;

    EosDirEntry &blocksLeft = entries[3];
    eos_set_name(blocksLeft, "BLOCKS LEFT");
    blocksLeft.attr = EosAttr::BLOCKS_LEFT;
    blocksLeft.start = EOS_DIR_BLOCKS + 1;
    blocksLeft.allocated = numBlocks - (EOS_DIR_BLOCKS + 1);
    blocksLeft.year = 0x57; // date EOS stamps its own template with
    blocksLeft.month = 0x07;
    blocksLeft.day = 0x11;

    for (uint32_t idx = 0; idx < EOS_DIR_BLOCKS; idx++)
    {
        memset(block, 0, sizeof(block));
        memcpy(block, &entries[idx * EOS_DIR_ENTRIES_PER_BLOCK],
               EOS_DIR_ENTRIES_PER_BLOCK * sizeof(EosDirEntry));
        if (_write_block_at(f, 1 + idx, block).is_error())
            RETURN_ERROR_AS_FALSE();
    }

    RETURN_SUCCESS_AS_TRUE();
}

void MediaType::unmount()
{
    if (_media_fileh != nullptr)
    {
        fnio::fclose(_media_fileh);
        _media_fileh = nullptr;
    }
}

mediatype_t MediaType::discover_mediatype(const char *filename)
{
    int l = strlen(filename);
    if (l > 4 && filename[l - 4] == '.')
    {
        // Check the last 3 characters of the string
        const char *ext = filename + l - 3;
        if (strcasecmp(ext, "DDP") == 0)
        {
            return MEDIATYPE_DDP;
        }
        else if (strcasecmp(ext, "DSK") == 0)
        {
            return MEDIATYPE_DSK;
        }
        else if (strcasecmp(ext, "ROM") == 0)
        {
            return MEDIATYPE_ROM;
        }
    }
    return MEDIATYPE_UNKNOWN;
}

#endif /* BUILD_ADAM */
