
#include "fnDirCache.h"

#include <cstring>
#include <algorithm>
#include "compat_string.h"

#include "utils.h"


bool _fsdir_sort_name_ascend(const fsdir_entry* left, const fsdir_entry* right)
{
    if (left->isDir == right->isDir)
        return strcasecmp(left->filename, right->filename) < 0;
    else
        return left->isDir;
}

bool _fsdir_sort_name_descend(const fsdir_entry* left, const fsdir_entry* right)
{
    if (left->isDir == right->isDir)
        return strcasecmp(left->filename, right->filename) > 0;
    else
        return left->isDir;
}

bool _fsdir_sort_time_ascend(const fsdir_entry* left, const fsdir_entry* right)
{
    if (left->isDir == right->isDir)
        return left->modified_time > right->modified_time;
    else
        return left->isDir;
}

bool _fsdir_sort_time_descend(const fsdir_entry* left, const fsdir_entry* right)
{
    if (left->isDir == right->isDir)
        return left->modified_time < right->modified_time;
    else
        return left->isDir;
}

typedef bool (*sort_fn_t)(const fsdir_entry* left, const fsdir_entry* right);


void DirCache::clear()
{
    _entries_filtered.clear();
    _entries_filtered.shrink_to_fit();
    _entries.clear();
    _entries.shrink_to_fit();
    _current = 0;
}

fsdir_entry &DirCache::new_entry()
{
    _entries.push_back(fsdir_entry());
    return _entries.back();
}

void DirCache::apply_filter(const char *pattern, uint16_t diropts)
{
	char realpat[MAX_PATHLEN];
	//char *thepat = nullptr;
    bool have_pattern = pattern != nullptr && pattern[0] != '\0';
	bool filter_dirs = have_pattern && pattern[strlen(pattern)-1] == '/';
	if (filter_dirs) {
		strlcpy (realpat, pattern, sizeof (realpat));
		realpat[strlen(realpat)-1] = '\0';
	}
	//thepat = filter_dirs ? realpat : (char *)pattern;

    _entries_filtered.clear();
    _entries_filtered.shrink_to_fit();

    // Filter directory entries
    for (unsigned i=0; i<_entries.size(); ++i)
    {
        fsdir_entry& entry = _entries[i];
        // Skip this entry if we have a search filter and it doesn't match it
        if (have_pattern && (
            !entry.isDir || (entry.isDir && filter_dirs)
            ) && util_wildcard_match(entry.filename, pattern) == false)
            continue;
        _entries_filtered.push_back(&entry);
    }

    // Choose the appropriate sorting function
    sort_fn_t sortfn;
    if (diropts & DIR_OPTION_FILEDATE)
    {
        sortfn = (diropts & DIR_OPTION_DESCENDING) ? _fsdir_sort_time_descend : _fsdir_sort_time_ascend;
    }
    else
    {
        sortfn = (diropts & DIR_OPTION_DESCENDING) ? _fsdir_sort_name_descend : _fsdir_sort_name_ascend;
    }

    // Sort directory entries
    std::sort(_entries_filtered.begin(), _entries_filtered.end(), sortfn);
    // rewind read cursor
    _current = 0;
}

fsdir_entry *DirCache::read()
{
    if(_current < _entries_filtered.size())
        return _entries_filtered[_current++];
    else
        return nullptr;
}

uint16_t DirCache::tell()
{
    if(_entries_filtered.empty())
        return FNFS_INVALID_DIRPOS;
    else
        return _current;
}

bool DirCache::seek(uint16_t pos)
{
    if(pos <= _entries_filtered.size())
    {
        _current = pos;
        return true;
    }
    else
        return false;
}
