#ifndef FN_DIRCACHE_H
#define FN_DIRCACHE_H

#include <cstring>
#include <vector>

#ifdef ESP_PLATFORM
#include "../../include/PSRAMAllocator.h"
#endif

#include "fnFSDirEntry.h"
#include "global_types.h"

class DirCache
{
private:
    // Fixed-size entry; the name lives in _names so a long path costs only its length
    struct Record
    {
        uint32_t name_offset;
        uint32_t size;
        uint32_t modified_time;
        bool isDir;
    };
    static_assert(sizeof(Record) == 16, "DirCache::Record must stay 16 bytes");

#ifdef ESP_PLATFORM
    std::vector<char,PSRAMAllocator<char>> _names;
    std::vector<Record,PSRAMAllocator<Record>> _records;
    std::vector<uint32_t,PSRAMAllocator<uint32_t>> _filtered;
#else
    std::vector<char> _names;
    std::vector<Record> _records;
    std::vector<uint32_t> _filtered;
#endif
    uint16_t _current = 0;
    // Directory the entries came from, with its NUL; empty if unset
#ifdef ESP_PLATFORM
    std::vector<char,PSRAMAllocator<char>> _path;
#else
    std::vector<char> _path;
#endif

    const char *name_of(uint32_t idx) const { return &_names[_records[idx].name_offset]; }

public:
#ifdef ESP_PLATFORM
    // Hold a listing in PSRAM, not the DRAM the default heap would give it. Returns nullptr when
    // PSRAM and DRAM are both full, which a new-expression checks for.
    static void *operator new(size_t size) noexcept
    {
        return PSRAMAllocator<char>().allocate(size);
    }
    static void operator delete(void *ptr) noexcept { heap_caps_free(ptr); }
#endif

    void clear();
    // Which directory the entries hold, so a caller can skip reading it again
    void set_path(const char *path);
    bool holds(const char *path) const { return !_path.empty() && strcmp(&_path[0], path) == 0; }
    // Listing positions are 16-bit, and FNFS_INVALID_DIRPOS is not one
    static constexpr size_t MAX_ENTRIES = FNFS_INVALID_DIRPOS - 1;

    // An error leaves the entries already added in place
    success_is_true add_entry(const char *name, bool isDir, uint32_t size, time_t modified_time);
    success_is_true add_entry(const fsdir_entry &entry)
    {
        return add_entry(entry.filename, entry.isDir, entry.size, entry.modified_time);
    }
    success_is_true apply_filter(const char *pattern, uint16_t diropts);

    bool empty() {return _records.empty();}

    // Fills out and returns it, or returns nullptr at the end
    fsdir_entry *read(fsdir_entry &out);
    uint16_t tell();
    success_is_true seek(uint16_t pos);
};

#endif // FN_DIRCACHE_H
