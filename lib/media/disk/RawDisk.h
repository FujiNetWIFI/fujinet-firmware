#ifndef RAWDISK_H
#define RAWDISK_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t RAW_DISK_SECTOR_SIZE = 512;

// A raw disk image: 512-byte sectors in order, with no header. It's the format
// of last resort, so it accepts a file of any name; a trailing partial sector
// is ignored. The image records no geometry, so it's taken from a FAT boot
// sector, else a PC floppy of the same size, else one side of 9-sector tracks.
class RawDisk : public BlockMedia
{
public:
    // Names the directory entry type only; attach() takes any name.
    static constexpr const char *extensions[] = {".img", ".xex"};

    RawDisk(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly) {}

    uint8_t sideCount() const override { return _sides; }
    uint16_t cylinderCount() const override { return _cylinders; }
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;
    uint32_t blockCount() const override { return _size / RAW_DISK_SECTOR_SIZE; }
    uint16_t blockSize(uint32_t block) const override;

    // Flushed straight away, since the computer may be reset at any moment.
    error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer) override;

    bool isReadOnly() const override { return _readOnly; }

    // Writes a blank image of numSectors sectors of sectorSize bytes to f.
    static success_is_true create(fnFile *f, uint16_t sectorSize, uint32_t numSectors);

protected:
    error_is_true attach() override;
    error_is_true blockToOffset(uint32_t block, uint32_t &offset) const override;

private:
    bool _readOnly;
    uint8_t _sides = 0;
    uint16_t _cylinders = 0;
    uint8_t _sectorsPerTrack = 0;
    TrackEncoding _encoding = TrackEncoding::UNKNOWN;

    success_is_true geometryFromBootSector();
    success_is_true geometryFromFloppySize();
    void setGeometry(uint16_t cylinders, uint8_t sides, uint8_t sectorsPerTrack,
                     TrackEncoding encoding);
};

#endif /* RAWDISK_H */
