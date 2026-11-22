#ifndef _MEDIATYPE_IMD
#define _MEDIATYPE_IMD

#include <memory>

#include "diskType.h"

#include "../IMDImage.h"
#include "../../include/PSRAMMemoryResource.h"

// Serves an ImageDisk (.imd) image using its native geometry: drive sector N is
// IMD LBA N and sector_size() reports the real per-sector size, so a mixed
// density disk (128-byte track 0, 512-byte data tracks) presents as it did on
// the original drive.
class MediaTypeIMD : public MediaType
{
public:
    static constexpr std::array<const char *, 1> extensions = { ".imd" };

    using MediaType::MediaType;

    mediatype_t mount(fnFile *f, uint32_t disksize) override;
    void unmount() override;

    error_is_true read(uint32_t sectornum, uint32_t *readcount) override;
    error_is_true write(uint32_t sectornum, bool verify) override;
    error_is_true format(uint32_t *responsesize) override;

    uint16_t sector_size(uint32_t sectornum) override;

    void status(uint8_t statusbuff[4]) override;

    // Only IBM-3740 (8" single density, 77 x 26 x 128), the Model II's format.
    static success_is_true create(fnFile *f, uint16_t sectorSize, uint32_t numSectors);

private:
    void _derive_geometry();
    void _fill_percom();
    uint32_t _lba_count() const;   // 0 when nothing is mounted

    PSRAMMemoryResource _psram;     // the sector tables; declared first so it outlives _imd
    std::unique_ptr<IMDImage> _imd; // made at mount, around the mounted handle
    uint8_t _base_ctrl_status = DISK_CTRL_STATUS_CLEAR;

    // Derived once at mount for the PERCOM block and status()
    uint16_t _cyls = 0;
    uint16_t _spt = 0;
    uint8_t _heads = 0;
    bool _mfm = false;
};

#endif // _MEDIATYPE_IMD
