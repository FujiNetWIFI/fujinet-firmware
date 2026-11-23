#ifndef COCODSK_H
#define COCODSK_H

#include "BlockMedia.h"

class fujiHost;

constexpr uint16_t COCO_DSK_SECTOR_SIZE = 256;
constexpr uint8_t COCO_DSK_SECTORS_TRACK = 18;
// Track 17, sector 18: unused by Disk BASIC
constexpr uint32_t COCO_DSK_HIGH_SCORE_BLOCK = 323;
constexpr uint8_t COCO_DSK_HIGH_SCORE_RANGES = 4;

// A headerless CoCo or Dragon disk: 256-byte sectors stored in LSN order. The
// image doesn't record its sides, so it's described as one side of 18-sector tracks.
class CoCoDSK : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".dsk"};

    // host reopens the image read-write for a high-score sector; may be nullptr.
    CoCoDSK(const std::string &path, fnFile *handle, fujiHost *host, bool readOnly)
        : BlockMedia(path, handle), _host(host), _readOnly(readOnly) {}

    uint8_t sideCount() const override { return 1; }
    uint16_t cylinderCount() const override;
    uint32_t blockCount() const override { return _size / COCO_DSK_SECTOR_SIZE; }
    uint16_t blockSize(uint32_t block) const override;

    // A high-score sector is writable even on a read-only mount.
    error_is_true writeBlock(uint32_t block, const ByteBuffer &buffer) override;

    bool isReadOnly() const override { return _readOnly; }

protected:
    error_is_true attach() override;
    error_is_true blockToOffset(uint32_t block, uint32_t &offset) const override;

private:
    struct HighScoreRange
    {
        uint16_t start;
        uint16_t count;
    };

    fujiHost *_host;
    bool _readOnly;
    uint8_t _highScoreRangeCount = 0;
    HighScoreRange _highScoreRanges[COCO_DSK_HIGH_SCORE_RANGES];

    void readHighScoreMarker();
    bool isHighScoreBlock(uint32_t block) const;
};

#endif /* COCODSK_H */
