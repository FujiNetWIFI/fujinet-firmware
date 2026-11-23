#ifndef DRAGONVDK_H
#define DRAGONVDK_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t DRAGON_VDK_SECTOR_SIZE = 256;

// A PC-Dragon VDK: a header that gives its own length, then 256-byte sectors.
// Dragon DOS numbers sectors from 1, so block 0 is refused and block 1 is the
// first sector; the last is blockCount() - 1.
class DragonVDK : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".vdk"};

    DragonVDK(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool readOnly)
        : BlockMedia(path, handle), _readOnly(readOnly) {}

    uint8_t sideCount() const override { return _sides; }
    uint16_t cylinderCount() const override { return _tracks; }
    uint32_t blockCount() const override { return _sectors + 1; }
    uint16_t blockSize(uint32_t block) const override;

    bool isReadOnly() const override { return _readOnly; }

protected:
    error_is_true attach() override;
    error_is_true blockToOffset(uint32_t block, uint32_t &offset) const override;

private:
    bool _readOnly;
    uint16_t _headerSize = 0;
    uint32_t _sectors = 0;
    uint8_t _tracks = 0;
    uint8_t _sides = 0;
};

#endif /* DRAGONVDK_H */
