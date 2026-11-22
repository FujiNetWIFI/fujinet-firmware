#ifndef BLOCKMEDIALEGACY_H
#define BLOCKMEDIALEGACY_H

#include "BlockMedia.h"
#include "rs232/mediatype.h"

class MediaType;

// FIXME - remove once every rs232 format derives BlockMedia directly.
// The BlockMedia contract in terms of the rs232 MediaType API, so the drive has one path.
class LegacyBlockMedia : public BlockMedia
{
public:
    LegacyBlockMedia(const std::string &path, fnFile *handle, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly) {}

    uint8_t sideCount() const override;
    uint16_t cylinderCount() const override;
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;
    uint32_t blockCount() const override;
    uint16_t blockSize(uint32_t block) const override;

    error_is_true readBlock(uint32_t block, ByteBuffer &buffer) override;
    error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer) override;

    bool isReadOnly() const override { return _readOnly; }
    error_is_true close() override;

protected:
    error_is_true attach() override;

private:
    bool _readOnly;

    // Only MediaType derives from this.
    MediaType &legacy() const;
};

#endif /* BLOCKMEDIALEGACY_H */
