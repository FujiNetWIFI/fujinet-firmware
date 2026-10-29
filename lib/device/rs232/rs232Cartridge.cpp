#ifdef BUILD_RS232

#include "rs232Cartridge.h"

#include "../../include/debug.h"
#include "compat_string.h"
#include "media.h"

bool rs232Cartridge::is_rom(const char *filename, uint32_t size)
{
    return MediaType::discover_mediatype(filename, size) == MEDIATYPE_ROM;
}

success_is_true rs232Cartridge::mount(fnFile *fileh, const char *filename, uint32_t size,
                                      fujiHost &host)
{
    MediaTypeROM rom; // closes the ROM and its map when it goes out of scope
    rom._media_host = &host;
    strlcpy(rom._disk_filename, filename, sizeof(rom._disk_filename));
    if (rom.mount(fileh, size) == MEDIATYPE_UNKNOWN)
    {
        Debug_printv("ROM push: failed to mount %s\n", filename);
        RETURN_ERROR_AS_FALSE();
    }

    auto reader = [&rom](RomStream stream) {
        return [&rom, stream](uint32_t offset, uint8_t *buffer, size_t length) {
            return rom.stream_read(stream, offset, buffer, length);
        };
    };

    if (rom.has_memory_map() &&
        push(STREAM_MEMORY_MAP, rom.stream_size(RomStream::MemoryMap),
             reader(RomStream::MemoryMap))
            .is_error())
    {
        Debug_printv("ROM push: memory map push failed\n");
        RETURN_ERROR_AS_FALSE();
    }

    return push(STREAM_IMAGE, rom.stream_size(RomStream::Image), reader(RomStream::Image));
}

#endif /* BUILD_RS232 */
