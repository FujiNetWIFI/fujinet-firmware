
#include "fnDirCache.h"

#include <cstring>
#include <algorithm>
#include "compat_string.h"

#include "utils.h"


void DirCache::clear()
{
    _filtered.clear();
    _filtered.shrink_to_fit();
    _records.clear();
    _records.shrink_to_fit();
    _names.clear();
    _names.shrink_to_fit();
    _path.clear();
    _path.shrink_to_fit();
    _current = 0;
}

void DirCache::set_path(const char *path)
{
    _path.assign(path, path + strlen(path) + 1);
}

namespace
{

// Leave this much of the heap to everything else
constexpr size_t HEAP_RESERVE = 32 * 1024;

// A vector reallocates with the old and new block alive together, and with exceptions off a
// failed allocation aborts, so check before growing. Host builds don't check.
template <class Vec> success_is_true fits(const Vec &vec, size_t new_capacity)
{
#ifdef ESP_PLATFORM
    if (new_capacity <= vec.capacity())
        RETURN_SUCCESS_AS_TRUE();
#if CONFIG_SPIRAM
    uint32_t caps = MALLOC_CAP_SPIRAM;
#else
    uint32_t caps = MALLOC_CAP_DEFAULT;
#endif
    RETURN_SUCCESS_IF(heap_caps_get_largest_free_block(caps) >=
                      new_capacity * sizeof(typename Vec::value_type) + HEAP_RESERVE);
#else
    RETURN_SUCCESS_AS_TRUE();
#endif
}

} // namespace

success_is_true DirCache::add_entry(const char *name, bool isDir, uint32_t size, time_t modified_time)
{
    size_t name_size = strlen(name) + 1;
    // vector::insert and push_back grow to size + max(size, added)
    if (_records.size() >= MAX_ENTRIES
        || fits(_records, _records.size() + std::max<size_t>(_records.size(), 1)).is_error()
        || fits(_names, _names.size() + std::max(_names.size(), name_size)).is_error())
        RETURN_ERROR_AS_FALSE();

    Record record;
    record.name_offset = _names.size();
    record.size = size;
    record.modified_time = (uint32_t)modified_time;
    record.isDir = isDir;

    _names.insert(_names.end(), name, name + name_size);
    _records.push_back(record);
    RETURN_SUCCESS_AS_TRUE();
}

success_is_true DirCache::apply_filter(const char *pattern, uint16_t diropts)
{
	char realpat[MAX_PATHLEN];
    bool have_pattern = pattern != nullptr && pattern[0] != '\0';
	bool filter_dirs = have_pattern && pattern[strlen(pattern)-1] == '/';
	if (filter_dirs) {
		strlcpy (realpat, pattern, sizeof (realpat));
		realpat[strlen(realpat)-1] = '\0';
	}
	const char *thepat = filter_dirs ? realpat : pattern;

    _filtered.clear();
    _filtered.shrink_to_fit();
    if (fits(_filtered, _records.size()).is_error())
        RETURN_ERROR_AS_FALSE();
    _filtered.reserve(_records.size());

    // Filter directory entries
    for (uint32_t idx = 0; idx < _records.size(); ++idx)
    {
        const Record &record = _records[idx];
        // Skip this entry if we have a search filter and it doesn't match it
        if (have_pattern && (
            !record.isDir || (record.isDir && filter_dirs)
            ) && util_wildcard_match(name_of(idx), thepat) == false)
            continue;
        _filtered.push_back(idx);
    }

    _current = 0;

    if (diropts & DIR_OPTION_UNSORTED)
        RETURN_SUCCESS_AS_TRUE();

    // Directories first, then by name or date
    bool by_date = diropts & DIR_OPTION_FILEDATE;
    bool descending = diropts & DIR_OPTION_DESCENDING;
    std::sort(_filtered.begin(), _filtered.end(), [&](uint32_t left, uint32_t right) {
        const Record &lrec = _records[left];
        const Record &rrec = _records[right];
        if (lrec.isDir != rrec.isDir)
            return lrec.isDir;
        if (by_date)
            return descending ? lrec.modified_time < rrec.modified_time
                              : lrec.modified_time > rrec.modified_time;
        int cmp = strcasecmp(name_of(left), name_of(right));
        return descending ? cmp > 0 : cmp < 0;
    });
    RETURN_SUCCESS_AS_TRUE();
}

fsdir_entry *DirCache::read(fsdir_entry &out)
{
    if (_current >= _filtered.size())
        return nullptr;

    uint32_t idx = _filtered[_current++];
    const Record &record = _records[idx];
    strlcpy(out.filename, name_of(idx), sizeof(out.filename));
    out.isDir = record.isDir;
    out.size = record.size;
    out.modified_time = record.modified_time;
    return &out;
}

uint16_t DirCache::tell()
{
    if(_filtered.empty())
        return FNFS_INVALID_DIRPOS;
    else
        return _current;
}

success_is_true DirCache::seek(uint16_t pos)
{
    if(pos <= _filtered.size())
    {
        _current = pos;
        RETURN_SUCCESS_AS_TRUE();
    }
    else
        RETURN_ERROR_AS_FALSE();
}
