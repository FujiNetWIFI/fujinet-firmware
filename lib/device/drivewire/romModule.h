#ifndef DRIVEWIREROMMODULE_H
#define DRIVEWIREROMMODULE_H

#include <cstdint>
#include <memory>
#include <string>

#include "MediaFormats.h"
#include "PSRAMMemoryResource.h"
#include "mediatype.h"
#include "rom/PMD85ROM.h"

class fujiHost;

// A PMD 85 ROM module mounted in a drive slot, which the computer reads a
// 32K page at a time with OP_READEX. It is not a disk, so the slot's
// drivewireDisk stays unmounted.
class drivewireROMModule
{
public:
    // What a slot holds as a module, and the type its directory entry reports.
    using Formats = MediaFormats<MEDIATYPE_UNKNOWN, MediaFormat<PMD85ROM, MEDIATYPE_MRM>>;

    enum class PageStatus : uint8_t { OK, PAST_END, READ_ERROR };

    // The module opened from path; nullptr if the page buffer can't be allocated.
    static std::unique_ptr<drivewireROMModule> create(std::unique_ptr<ROMMedia> rom,
                                                      fujiHost &host, const std::string &path);
    ~drivewireROMModule();

    drivewireROMModule(const drivewireROMModule &) = delete;
    drivewireROMModule &operator=(const drivewireROMModule &) = delete;

    // Loads a page into page(), padded with $FF past the end of the file.
    PageStatus load(uint32_t pageNumber);

    // The loaded page, for the bus to send from.
    uint8_t *page() { return _page; }
    static constexpr size_t pageSize() { return PMD85ROM::PAGE_SIZE; }

private:
    drivewireROMModule(std::unique_ptr<ROMMedia> rom, fujiHost &host, const std::string &path,
                       uint8_t *page)
        : _rom(std::move(rom)), _host(host), _path(path), _page(page), _size(_rom->size()) {}

    void closeFile();
    // Closes the file and opens it again from the start.
    error_is_true rewind();
    // Reads the next page in sequence into pageBuffer, or past it if that is nullptr.
    error_is_true readNext(uint8_t *pageBuffer);

    PSRAMMemoryResource _memory;
    std::unique_ptr<ROMMedia> _rom;
    fujiHost &_host;
    std::string _path;
    uint8_t *_page;
    size_t _size;
    uint32_t _next = 0;
    uint32_t _loaded = UINT32_MAX;
};

#endif /* DRIVEWIREROMMODULE_H */
