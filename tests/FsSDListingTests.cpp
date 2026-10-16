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

std::vector<std::string> read_all(FileSystemSDFAT &sd)
{
    std::vector<std::string> out;
    fsdir_entry *e;
    while ((e = sd.dir_read()) != nullptr)
        out.push_back(e->filename);
    return out;
}

} // namespace

TEST_CASE("dir_open puts directories first, sorted, and hides dotfiles and fnconfig.ini")
{
    make_sd_root();

    REQUIRE(fnSDFAT.dir_open("/a", nullptr, 0).is_success());
    CHECK(read_all(fnSDFAT) == std::vector<std::string>{"sub", "a.atr", "b.atr"});
    REQUIRE(fnSDFAT.dir_seek(0).is_success());
    CHECK(fnSDFAT.dir_read()->isDir);
    CHECK_FALSE(fnSDFAT.dir_read()->isDir);
    fnSDFAT.dir_close();
}

TEST_CASE("dir_open on a missing directory fails and leaves no listing")
{
    make_sd_root();

    REQUIRE(fnSDFAT.dir_open("/a", nullptr, 0).is_success());
    CHECK(fnSDFAT.dir_open("/missing", nullptr, 0).is_error());
    CHECK(fnSDFAT.dir_read() == nullptr);
    fnSDFAT.dir_close();
}

TEST_CASE("a second handle's listing does not disturb fnSDFAT's")
{
    make_sd_root();

    REQUIRE(fnSDFAT.dir_open("/a", nullptr, 0).is_success());
    fsdir_entry *e = fnSDFAT.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "sub");
    uint16_t pos = fnSDFAT.dir_tell();

    // What the web UI does between a client's OPEN DIRECTORY and READ DIRECTORY.
    FileSystemSDFAT other(fnSDFAT);
    CHECK(other.running());
    REQUIRE(other.dir_open("/b", nullptr, 0).is_success());
    CHECK(read_all(other) == std::vector<std::string>{"c.atr"});
    other.dir_close();

    CHECK(fnSDFAT.dir_tell() == pos);
    CHECK(read_all(fnSDFAT) == std::vector<std::string>{"a.atr", "b.atr"});
    fnSDFAT.dir_close();
}
