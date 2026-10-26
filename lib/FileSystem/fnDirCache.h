#ifndef FN_DIRCACHE_H
#define FN_DIRCACHE_H

#include <vector>

#ifdef ESP_PLATFORM
#include "../../include/PSRAMAllocator.h"
#endif

#include "fnFS.h"

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
    // Returned by read(); valid until the next read() or clear()
    fsdir_entry _entry;

    const char *name_of(uint32_t idx) const { return &_names[_records[idx].name_offset]; }

public:
    void clear();
    void add_entry(const fsdir_entry &entry);
    void apply_filter(const char *pattern, uint16_t diropts);

    bool empty() {return _records.empty();}

    fsdir_entry *read();
    uint16_t tell();
    success_is_true seek(uint16_t pos);
};

#endif // FN_DIRCACHE_H
