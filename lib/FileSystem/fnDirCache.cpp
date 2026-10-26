
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
    _current = 0;
}

void DirCache::add_entry(const fsdir_entry &entry)
{
    Record record;
    record.name_offset = _names.size();
    record.size = entry.size;
    record.modified_time = (uint32_t)entry.modified_time;
    record.isDir = entry.isDir;

    _names.insert(_names.end(), entry.filename, entry.filename + strlen(entry.filename) + 1);
    _records.push_back(record);
}

void DirCache::apply_filter(const char *pattern, uint16_t diropts)
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
        return;

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
}

fsdir_entry *DirCache::read()
{
    if (_current >= _filtered.size())
        return nullptr;

    uint32_t idx = _filtered[_current++];
    const Record &record = _records[idx];
    strlcpy(_entry.filename, name_of(idx), sizeof(_entry.filename));
    _entry.isDir = record.isDir;
    _entry.size = record.size;
    _entry.modified_time = record.modified_time;
    return &_entry;
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
