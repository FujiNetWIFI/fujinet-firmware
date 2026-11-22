#ifndef IMDDISK_H
#define IMDDISK_H

#include "BlockMedia.h"
#include "IMDImage.h"
#include "PSRAMMemoryResource.h"

class fujiHost;

// An ImageDisk (.imd) image in its native geometry: block N is IMD LBA N and
// has that sector's own size, so a 128-byte track 0 stays 128 bytes. Records
// are compressed, so blocks are reached through IMDImage, not by offset.
class IMDDisk : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".imd"};

    IMDDisk(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly), _image(handle, &_psram) {}

    uint8_t sideCount() const override { return _sides; }
    uint16_t cylinderCount() const override { return _cylinders; }
    // From the track's header, sized by its first sector; {0, 0} if the image
    // has no such track.
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;
    uint32_t blockCount() const override { return _image.lbaCount(); }
    uint16_t blockSize(uint32_t block) const override { return _image.sectorSize(block); }

    // A record marked as a data error reads back its recovered contents; one
    // that was never readable on the original disk fails.
    error_is_true readBlock(uint32_t block, ByteBuffer &buffer) override;
    // Flushed by IMDImage before it returns.
    error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer) override;

    bool isReadOnly() const override { return _readOnly; }
    error_is_true close() override;

    // Only IBM-3740 (8" single density, 77 x 26 x 128), the Model II's format.
    static success_is_true create(fnFile *f, uint16_t sectorSize, uint32_t numSectors);

protected:
    // Any name but a raw image's is read for an IMD header.
    error_is_true attach() override;

private:
    // Names that are raw sector images, so never worth a read.
    static constexpr const char *rawExtensions[] = {".img", ".dsk"};

    bool _readOnly;
    PSRAMMemoryResource _psram; // the sector tables; declared first so it outlives _image
    IMDImage _image;
    uint8_t _sides = 0;
    uint16_t _cylinders = 0;
};

#endif /* IMDDISK_H */
