#ifndef _MEDIATYPE_ROM_RS232
#define _MEDIATYPE_ROM_RS232

#include "diskType.h"

// A ROM-shaped file (Intellivision .bin+.cfg pair, Intellicart .rom, Atari
// 2600 .bin, CoCo cartridge image, ...).
//
// An Intellivision ROM's memory map travels beside it as a same-named .cfg;
// which extension that is, and in what casing a host may hold it, is format
// knowledge and lives here. mount() resolves the sibling; cfg_open() hands
// the caller a read handle. Moving the bytes is the device layer's job.
class MediaTypeROM : public MediaType
{
public:
    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;

    error_is_true format(uint32_t *responsesize) override;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    void status(uint8_t statusbuff[4]) override;

    // True when this ROM has a memory map beside it. A ROM without one is
    // normal; a ROM whose map is present but unreadable is not, so callers
    // treat a null cfg_open() after has_cfg() as a failed mount.
    bool has_cfg() const { return _cfg_path[0] != '\0'; }

    // Opens the resolved map, or returns nullptr with *size 0 on failure.
    fnFile *cfg_open(uint32_t *size);
    void cfg_close(fnFile *f);

private:
    void resolve_cfg();

    char _cfg_path[256] = {0};
};

#endif // _MEDIATYPE_ROM_RS232
