#ifdef BUILD_COCO

#include "drivewireCartridge.h"

#include "../../include/debug.h"
#include "compat_string.h"
#include "media.h"

bool drivewireCartridge::is_rom(const char *filename, uint32_t)
{
    return MediaType::discover_mediatype(filename) == MEDIATYPE_ROM;
}

success_is_true drivewireCartridge::mount(fnFile *fileh, const char *filename, uint32_t size,
                                          fujiHost &)
{
    MediaTypeROM rom; // closes the ROM when it goes out of scope
    strlcpy(rom._disk_filename, filename, sizeof(rom._disk_filename));
    if (rom.mount(fileh, size) == MEDIATYPE_UNKNOWN)
    {
        Debug_printv("ROM push: failed to mount %s\n", filename);
        RETURN_ERROR_AS_FALSE();
    }

    return push(0, rom.stream_size(), [&rom](uint32_t offset, uint8_t *buffer, size_t length) {
        return rom.stream_read(offset, buffer, length);
    });
}

#endif /* BUILD_COCO */
