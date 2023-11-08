#ifndef FN_DIRCACHE_H
#define FN_DIRCACHE_H

#include <vector>

#include "fnFS.h"

class DirCache
{
private:
    std::vector<fsdir_entry> _entries;
    std::vector<fsdir_entry> _entries_filtered;
    uint16_t _current = 0;

public:
    // DirCache();
    // ~DirCache();

    void clear();
    fsdir_entry &new_entry();
    void apply_filter(const char *pattern, uint16_t diropts);

    bool empty() {return _entries.empty();}

    fsdir_entry *read();
    uint16_t tell();
    bool seek(uint16_t pos);
};

#endif // FN_DIRCACHE_H