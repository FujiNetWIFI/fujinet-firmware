#ifndef PMD85ROM_H
#define PMD85ROM_H

#include "ROMMedia.h"

#include <cstdint>
#include <memory>

// A PMD 85 ROM module. The computer reads it a page at a time, so it is not
// pushed to a cartridge whole like the other ROMs.
class PMD85ROM : public ROMMedia
{
public:
    static constexpr size_t PAGE_SIZE = 32_kB;
    static constexpr size_t MAX_PAGES = 256;

protected:
    error_is_true attach() override;

public:
    PMD85ROM(const std::string &path, fnFile *handle, fujiHost *host)
        : ROMMedia(path, handle, host) {}

    size_t pageCount() const { return (_size + PAGE_SIZE - 1) / PAGE_SIZE; }

    // The page buffer, valid from attach() until the object goes away.
    uint8_t *pageBuffer() const { return _page.get(); }

    // Loads a page into pageBuffer(), padded with $FF past the end of the file;
    // an error if the page is out of range or the file can't be read.
    error_is_true readPage(uint32_t page);

private:
    struct PageFree
    {
        void operator()(uint8_t *ptr) const;
    };

    std::unique_ptr<uint8_t, PageFree> _page;
    uint32_t _loaded = UINT32_MAX;
};

#endif /* PMD85ROM_H */
