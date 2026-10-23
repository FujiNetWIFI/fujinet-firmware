#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>

#include "fnDirCache.h"
#include "samlib.h"

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

namespace
{

// Insertion, name and date order all differ, so the result shows which sort ran
void fill(DirCache &cache)
{
    const char *names[] = {"charlie", "alpha", "bravo"};
    const time_t dates[] = {200, 300, 100};
    for (int idx = 0; idx < 3; idx++)
    {
        fsdir_entry &entry = cache.new_entry();
        strcpy(entry.filename, names[idx]);
        entry.isDir = false;
        entry.size = 0;
        entry.modified_time = dates[idx];
    }
}

std::string order(DirCache &cache, uint16_t diropts)
{
    cache.apply_filter(nullptr, diropts);
    std::string names;
    for (fsdir_entry *entry = cache.read(); entry != nullptr; entry = cache.read())
        names += std::string(entry->filename) + " ";
    return names;
}

} // namespace

// #2140: DIR_OPTION_FILEDATE was 0x0003, so DESCENDING sorted by date and FILEDATE didn't sort
TEST_CASE("each directory option sorts the way it says")
{
    DirCache cache;
    fill(cache);

    CHECK(order(cache, 0) == "alpha bravo charlie ");
    CHECK(order(cache, DIR_OPTION_UNSORTED) == "charlie alpha bravo ");
    CHECK(order(cache, DIR_OPTION_DESCENDING) == "charlie bravo alpha ");
    CHECK(order(cache, DIR_OPTION_FILEDATE) == "alpha charlie bravo ");
    CHECK(order(cache, DIR_OPTION_FILEDATE | DIR_OPTION_DESCENDING) == "bravo charlie alpha ");
}
