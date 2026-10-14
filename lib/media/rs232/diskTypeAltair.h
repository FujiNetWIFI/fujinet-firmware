#ifndef _MEDIATYPE_ALTAIR
#define _MEDIATYPE_ALTAIR

#include "diskType.h"

#define ALTAIR_SECTOR_SIZE 137

// Serves an Altair 88-DCDD / FDC+ image (.dsk): headerless 137-byte sectors in
// physical order, so drive sector N is track N / spt, sector N % spt. The FDC+
// card on the far side runs the drive timing and only ever moves whole sectors.
class MediaTypeAltair : public MediaType
{
public:
    // `writable` comes from the mount slot's access mode, which also decided
    // whether the file was opened "rb" or "rb+", so the two must agree.
    explicit MediaTypeAltair(bool writable) : _writable(writable) {}

    // .DSK is also a plain sector image, so only the exact FDC+ sizes qualify.
    static bool looks_like_altair(const char *filename, uint32_t disksize);

    mediatype_t mount(fnFile *f, uint32_t disksize) override;

    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;

    uint16_t sector_size(uint32_t sectornum) override;

    void status(uint8_t statusbuff[4]) override;

private:
    error_is_true _seek(uint32_t sectornum);

    bool _writable = false;
    uint16_t _tracks = 0;
    uint16_t _spt = 0;
};

#endif // _MEDIATYPE_ALTAIR
