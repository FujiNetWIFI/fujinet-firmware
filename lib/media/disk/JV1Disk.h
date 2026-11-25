#ifndef JV1DISK_H
#define JV1DISK_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t JV1_SECTOR_SIZE = 256;
constexpr uint8_t JV1_SECTORS_TRACK = 10;
constexpr uint32_t JV1_TRACK_SIZE = JV1_SECTOR_SIZE * JV1_SECTORS_TRACK;
constexpr uint16_t JV1_MAX_CYLINDERS = 96;

// A TRS-80 Model I JV1 image: headerless single-density tracks, ten 256-byte FM
// sectors each, on one side. A .jv1 may hold any whole number of tracks; a
// .dsk is taken only at 35, 40 or 80, since other formats share the extension.
class JV1Disk : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".jv1", ".dsk"};

    JV1Disk(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly) {}

    uint8_t sideCount() const override { return 1; }
    uint16_t cylinderCount() const override { return _cylinders; }
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;
    uint32_t blockCount() const override { return (uint32_t)_cylinders * JV1_SECTORS_TRACK; }
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
};

#endif /* JV1DISK_H */
