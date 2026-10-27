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

fsdir_entry *read_next(DirCache &cache)
{
    static fsdir_entry entry;
    return cache.read(entry);
}

// Insertion, name and date order all differ, so the result shows which sort ran
void fill(DirCache &cache)
{
    const char *names[] = {"charlie", "alpha", "bravo"};
    const time_t dates[] = {200, 300, 100};
    for (int idx = 0; idx < 3; idx++)
    {
        fsdir_entry entry = {};
        strcpy(entry.filename, names[idx]);
        entry.modified_time = dates[idx];
        cache.add_entry(entry);
    }
}

std::string order(DirCache &cache, uint16_t diropts)
{
    cache.apply_filter(nullptr, diropts);
    std::string names;
    for (fsdir_entry *entry = read_next(cache); entry != nullptr; entry = read_next(cache))
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

TEST_CASE("an entry reads back as it was added")
{
    DirCache cache;
    fsdir_entry entry = {};
    strcpy(entry.filename, "games");
    entry.isDir = true;
    cache.add_entry(entry);

    memset(&entry, 0, sizeof(entry));
    strcpy(entry.filename, "disk.atr");
    entry.size = 92160;
    entry.modified_time = 1700000000;
    cache.add_entry(entry);

    cache.apply_filter(nullptr, 0);
    fsdir_entry *got = read_next(cache);
    REQUIRE(got != nullptr);
    CHECK(std::string(got->filename) == "games");
    CHECK(got->isDir);

    got = read_next(cache);
    REQUIRE(got != nullptr);
    CHECK(std::string(got->filename) == "disk.atr");
    CHECK_FALSE(got->isDir);
    CHECK(got->size == 92160);
    CHECK(got->modified_time == 1700000000);

    CHECK(read_next(cache) == nullptr);
}

TEST_CASE("a name of the longest length survives")
{
    DirCache cache;
    fsdir_entry entry = {};
    memset(entry.filename, 'x', MAX_PATHLEN - 1);
    cache.add_entry(entry);

    cache.apply_filter(nullptr, 0);
    fsdir_entry *got = read_next(cache);
    REQUIRE(got != nullptr);
    CHECK(strlen(got->filename) == MAX_PATHLEN - 1);
}

TEST_CASE("filter, tell and seek work over the filtered listing")
{
    DirCache cache;
    fill(cache);
    cache.apply_filter("*a", 0);

    // "bravo" and "charlie" fall out, "alpha" stays
    CHECK(cache.tell() == 0);
    CHECK(std::string(read_next(cache)->filename) == "alpha");
    CHECK(cache.tell() == 1);
    CHECK(read_next(cache) == nullptr);

    CHECK(cache.seek(0).is_success());
    CHECK(std::string(read_next(cache)->filename) == "alpha");
    CHECK(cache.seek(2).is_error());
}

TEST_CASE("clear empties the listing")
{
    DirCache cache;
    fill(cache);
    cache.apply_filter(nullptr, 0);
    cache.clear();

    CHECK(cache.empty());
    CHECK(read_next(cache) == nullptr);
    CHECK(cache.tell() == FNFS_INVALID_DIRPOS);
}

TEST_CASE("a pattern ending in / filters directories as well as files")
{
    DirCache cache;
    const struct { const char *name; bool isDir; } items[] = {
        {"games", true}, {"docs", true}, {"gamer.atr", false}, {"notes.txt", false},
    };
    for (const auto &item : items)
    {
        fsdir_entry entry = {};
        strcpy(entry.filename, item.name);
        entry.isDir = item.isDir;
        cache.add_entry(entry);
    }

    cache.apply_filter("g*/", 0);
    CHECK(std::string(read_next(cache)->filename) == "games");
    CHECK(std::string(read_next(cache)->filename) == "gamer.atr");
    CHECK(read_next(cache) == nullptr);

    // Without the slash only files are filtered
    cache.apply_filter("*.atr", 0);
    CHECK(std::string(read_next(cache)->filename) == "docs");
    CHECK(std::string(read_next(cache)->filename) == "games");
    CHECK(std::string(read_next(cache)->filename) == "gamer.atr");
    CHECK(read_next(cache) == nullptr);
}

TEST_CASE("add_entry refuses an entry past the 16-bit position limit")
{
    DirCache cache;
    for (size_t idx = 0; idx < DirCache::MAX_ENTRIES; idx++)
        REQUIRE(cache.add_entry(std::to_string(idx).c_str(), false, 0, 0).is_success());

    CHECK(cache.add_entry("one too many", false, 0, 0).is_error());

    // The entries already added still list, and the end position is a valid one
    REQUIRE(cache.apply_filter(nullptr, DIR_OPTION_UNSORTED).is_success());
    size_t count = 0;
    while (read_next(cache) != nullptr)
        count++;
    CHECK(count == DirCache::MAX_ENTRIES);
    CHECK(cache.tell() == DirCache::MAX_ENTRIES);
    CHECK(cache.tell() != FNFS_INVALID_DIRPOS);
}

TEST_CASE("a cache knows which directory it holds")
{
    DirCache cache;
    CHECK_FALSE(cache.holds(""));

    cache.set_path("/games");
    CHECK(cache.holds("/games"));
    CHECK_FALSE(cache.holds("/docs"));

    cache.clear();
    CHECK_FALSE(cache.holds("/games"));
}
