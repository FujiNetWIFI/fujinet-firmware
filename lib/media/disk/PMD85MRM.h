#ifndef PMD85MRM_H
#define PMD85MRM_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t PMD85_MRM_BLOCK_SIZE = 32768;

// A PMD 85 ROM module (.rmm, or an .mrm Mega ROM Module), which the PMD 85
// reads from a disk slot a 32K page at a time; the last page is padded with $FF.
// It isn't a disk, so it reports no sides or cylinders (#2490).
class PMD85MRM : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".mrm", ".rmm"};

    PMD85MRM(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool /*readOnly*/)
        : BlockMedia(path, handle) {}

    uint8_t sideCount() const override { return 0; }
    uint16_t cylinderCount() const override { return 0; }
    uint32_t blockCount() const override;
    uint16_t blockSize(uint32_t block) const override;

    error_is_true readBlock(uint32_t block, ByteBuffer &buffer) override;

    bool isReadOnly() const override { return true; }

protected:
    error_is_true attach() override;
    error_is_true blockToOffset(uint32_t block, uint32_t &offset) const override;
};

#endif /* PMD85MRM_H */
