#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fnFsSD.h"

// fnFsSD.cpp only calls this with a pattern, and these tests pass none.
bool util_wildcard_match(const char *, const char *) { return true; }

namespace
{

namespace fs = std::filesystem;

// Relative to the ctest working directory rather than a temp dir, as in
// IMDImageTests.cpp, so it behaves the same under MSYS2.
#define SD_ROOT "fssd_listing_test.tmp"

void touch(const fs::path &p)
{
    std::ofstream(p) << "x";
}

// /a holds a directory, two images and two names the listing hides; /b one image.
void make_sd_root()
{
    static bool made = false;
    if (made)
        return;

    fs::remove_all(SD_ROOT);
    fs::create_directories(SD_ROOT "/a/sub");
    fs::create_directories(SD_ROOT "/b");
    touch(SD_ROOT "/a/b.atr");
    touch(SD_ROOT "/a/a.atr");
    touch(SD_ROOT "/a/.dot");
    touch(SD_ROOT "/a/fnconfig.ini");
    touch(SD_ROOT "/b/c.atr");

    REQUIRE(fnSDFAT.start(SD_ROOT).is_success());
    made = true;
}

std::vector<std::string> names(const std::vector<fsdir_entry> &entries)
{
    std::vector<std::string> out;
    for (const fsdir_entry &e : entries)
        out.push_back(e.filename);
    return out;
}

} // namespace

TEST_CASE("dir_list puts directories first, sorted, and hides dotfiles and fnconfig.ini")
{
    make_sd_root();

    std::vector<fsdir_entry> entries;
    REQUIRE(fnSDFAT.dir_list("/a", nullptr, 0, entries).is_success());

    CHECK(names(entries) == std::vector<std::string>{"sub", "a.atr", "b.atr"});
    CHECK(entries[0].isDir);
    CHECK_FALSE(entries[1].isDir);
}

TEST_CASE("dir_list on a missing directory fails and leaves the vector empty")
{
    make_sd_root();

    std::vector<fsdir_entry> entries(1);
    CHECK(fnSDFAT.dir_list("/missing", nullptr, 0, entries).is_error());
    CHECK(entries.empty());
}

TEST_CASE("dir_list does not disturb an open dir_open listing")
{
    make_sd_root();

    REQUIRE(fnSDFAT.dir_open("/a", nullptr, 0).is_success());
    fsdir_entry *e = fnSDFAT.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "sub");
    uint16_t pos = fnSDFAT.dir_tell();

    // What the web UI does between a client's OPEN DIRECTORY and READ DIRECTORY.
    std::vector<fsdir_entry> other;
    REQUIRE(fnSDFAT.dir_list("/b", nullptr, 0, other).is_success());
    CHECK(names(other) == std::vector<std::string>{"c.atr"});

    CHECK(fnSDFAT.dir_tell() == pos);
    e = fnSDFAT.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "a.atr");
    e = fnSDFAT.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "b.atr");
    CHECK(fnSDFAT.dir_read() == nullptr);

    fnSDFAT.dir_close();
}
