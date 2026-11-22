#ifndef _BLOCK_MEDIA_
#define _BLOCK_MEDIA_

#include <cstdint>
#include <optional>

#include "DiskMedia.h"

// How a track's bits were recorded, where the image says.
enum class TrackEncoding : uint8_t
{
    UNKNOWN,
    FM,
    MFM,
};

struct TrackGeometry
{
    uint8_t sectorCount;
    uint16_t sectorSize; // nominal; see blockSize() for exceptions
    TrackEncoding encoding = TrackEncoding::UNKNOWN;
};

struct CHS
{
    uint16_t cylinder;
    uint8_t side;
    uint8_t sector;
};

// All numbering is zero-based.
class BlockMedia : public DiskMedia
{
public:
    BlockMedia(const std::string &path, fnFile *handle) : DiskMedia(path, handle) {}

    // Defaults to no sectors, for a flat image that overrides blockCount(),
    // blockSize(), and blockToOffset() instead of describing tracks.
    virtual TrackGeometry trackGeometry(uint16_t /*cylinder*/, uint8_t /*side*/) const
    {
        return {0, 0};
    }

    // Defaults to the sum of sectorCount over every track.
    virtual uint32_t blockCount() const;

    // Defaults to the nominal sector size of the track holding block; 0 if
    // out of range. Override where sizes differ within a track.
    virtual uint16_t blockSize(uint32_t block) const;

    // Resizes buffer to blockSize(block).
    virtual error_is_true readBlock(uint32_t block, ByteBuffer &buffer);

    // Fails unless buffer.size() equals blockSize(block). Success means the
    // data was accepted; close() makes it durable.
    virtual error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer);

protected:
    // Walks tracks with side varying fastest. Override only if a format's
    // block numbering doesn't follow that order.
    virtual error_is_true blockToCHS(uint32_t block, CHS &chs) const;

    // No generic default: depends on the format's byte layout.
    virtual error_is_true offsetFromCHS(const CHS &/*chs*/, uint32_t &/*offset*/) const
    {
        RETURN_ERROR_AS_TRUE();
    }

    // A flat image overrides this to skip CHS altogether.
    virtual error_is_true blockToOffset(uint32_t block, uint32_t &offset) const;

    // A subclass that moves _fileh itself calls this, so the next block access seeks.
    void forgetPosition() { _nextOffset.reset(); }

private:
    // Where the last block access left the handle, so the next block in order
    // needs no seek. stdio needs a seek between a read and a write, so the
    // direction must match too.
    std::optional<uint32_t> _nextOffset;
    bool _lastWasWrite = false;

    error_is_true seekFor(uint32_t offset, bool writing);
};

#endif // _BLOCK_MEDIA_
