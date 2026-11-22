#ifndef ALTAIRDSK_H
#define ALTAIRDSK_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t ALTAIR_DSK_SECTOR_SIZE = 137;

// An Altair 88-DCDD / FDC+ image: headerless 137-byte FM sectors in physical
// order on one side. A .dsk is also a plain sector image, so only the exact
// FDC+ sizes are taken, and those fix the geometry. Padding after the last
// sector is never addressed.
class AltairDSK : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".dsk"};

    AltairDSK(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly) {}

    uint8_t sideCount() const override { return 1; }
    uint16_t cylinderCount() const override { return _cylinders; }
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;
    uint32_t blockCount() const override { return (uint32_t)_cylinders * _sectorsPerTrack; }
    uint16_t blockSize(uint32_t block) const override;

    // Flushed straight away, since the computer may be reset at any moment.
    error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer) override;

    bool isReadOnly() const override { return _readOnly; }

protected:
    error_is_true attach() override;
    error_is_true blockToOffset(uint32_t block, uint32_t &offset) const override;

private:
    bool _readOnly;
    uint16_t _cylinders = 0;
    uint8_t _sectorsPerTrack = 0;
};

#endif /* ALTAIRDSK_H */
