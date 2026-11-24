#ifndef COCOCAS_H
#define COCOCAS_H

#include "BlockMedia.h"
#include "casSourceFile.h"
#include "casIndex.h"
#include "decbLayout.h"

class fujiHost;

// A CoCo .cas tape presented as a read-only 35-track Disk BASIC floppy. The
// tape is indexed once at attach(); each sector is built from that index when
// it's read, so the image is never held in memory.
class CoCoCAS : public BlockMedia
{
public:
    static constexpr const char *extensions[] = {".cas"};

    CoCoCAS(const std::string &path, fnFile *handle, fujiHost * /*host*/, bool /*readOnly*/)
        : BlockMedia(path, handle) {}

    uint8_t sideCount() const override { return 1; }
    uint16_t cylinderCount() const override { return DECB_TRACKS; }
    TrackGeometry trackGeometry(uint16_t cylinder, uint8_t side) const override;

    error_is_true readBlock(uint32_t block, ByteBuffer &buffer) override;

    bool isReadOnly() const override { return true; }

protected:
    error_is_true attach() override;

private:
    CasSourceFile _source;
    CasReader _reader{&_source};
    CasIndex _index;
    DecbLayout _layout;
};

#endif /* COCOCAS_H */
