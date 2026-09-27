#ifndef _MEDIATYPE_ROM_RS232
#define _MEDIATYPE_ROM_RS232

#include "diskType.h"

enum class RomStream : uint8_t
{
    Image,
    MemoryMap, // how the image is banked; a same-named .cfg on Intellivision
};

// A ROM-shaped file (Intellivision .bin+.cfg pair, Intellicart .rom, Atari
// 2600 .bin, CoCo cartridge image, ...). Callers get bytes by offset, never a
// file handle.
class MediaTypeROM : public MediaType
{
public:
    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;

    error_is_true format(uint32_t *responsesize) override;

    mediatype_t mount(fnFile *fileh, uint32_t disksize) override;
    void unmount() override;

    void status(uint8_t statusbuff[4]) override;

    // A ROM without a map is normal; one whose map won't read is not.
    bool has_memory_map() const { return _map_path[0] != '\0'; }

    // 0 when the stream is absent or unreadable.
    uint32_t stream_size(RomStream source);

    // Returns bytes copied; 0 at end of stream or on error.
    size_t stream_read(RomStream source, uint32_t offset, uint8_t *buffer, size_t length);

    ~MediaTypeROM() override;

private:
    void resolve_memory_map();
    bool open_memory_map();
    void close_memory_map();

    char _map_path[256] = {0};
    fnFile *_map_fileh = nullptr;
    uint32_t _map_size = 0;
    uint32_t _map_pos = 0;

    uint32_t _image_pos = UINT32_MAX; // forces a seek on the first read
};

#endif // _MEDIATYPE_ROM_RS232
