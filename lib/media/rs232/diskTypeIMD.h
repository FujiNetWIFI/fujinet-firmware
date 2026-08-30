#ifndef _MEDIATYPE_IMD
#define _MEDIATYPE_IMD

#include "diskType.h"

#include "../IMDImage.h"

// Serves an ImageDisk (.imd) image using its native geometry: drive sector N is
// IMD LBA N and sector_size() reports the real per-sector size, so a mixed
// density disk (128-byte track 0, 512-byte data tracks) presents as it did on
// the original drive.
class MediaTypeIMD : public MediaType
{
public:
    // `writable` comes from the mount slot's access mode, which also decided
    // whether the file was opened "rb" or "rb+", so the two must agree.
    explicit MediaTypeIMD(bool writable) : _writable(writable) {}

    mediatype_t mount(fnFile *f, uint32_t disksize, fujiHost *host = nullptr,
                      const char *filename = nullptr) override;
    void unmount() override;

    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;
    error_is_true format(uint32_t *responsesize) override;

    uint16_t sector_size(uint32_t sectornum) override;

    void status(uint8_t statusbuff[4]) override;

private:
    void _derive_geometry();
    void _fill_percom();

    IMDImage _imd;
    bool _writable = false;
    uint8_t _base_ctrl_status = DISK_CTRL_STATUS_CLEAR;

    // Derived once at mount for the PERCOM block and status()
    uint16_t _cyls = 0;
    uint16_t _spt = 0;
    uint8_t _heads = 0;
    bool _mfm = false;
};

#endif // _MEDIATYPE_IMD
