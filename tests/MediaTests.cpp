#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "media.h"
#include "romMountStubs.h"
#include "fujiHost.h"

namespace
{

void add_host_file(const char *path, const std::string &contents)
{
    host_files.files[path] = std::vector<uint8_t>(contents.begin(), contents.end());
}

// A format that records where attach() found the handle and what it was built
// with, and accepts or refuses as told.
class ProbeMedia : public Media
{
public:
    ProbeMedia(const std::string &path, fnFile *handle, int extra = 0, bool accept = true)
        : Media(path, handle), extra(extra), accept(accept) {}

    int extra;
    bool accept;
    long positionAtAttach = -1;

    error_is_true close() override { RETURN_SUCCESS_AS_FALSE(); }
    bool isReadOnly() const override { return true; }

protected:
    error_is_true attach() override
    {
        positionAtAttach = fnio::ftell(_fileh);
        RETURN_ERROR_IF(!accept);
    }
};

} // namespace

TEST_CASE("open() rewinds the handle before attach()")
{
    MemFile file(make_image(64));
    fnio::fseek(&file, 40, SEEK_SET);

    auto media = Media::open<ProbeMedia>("cart.bin", &file);
    REQUIRE(media != nullptr);
    CHECK(media->positionAtAttach == 0);
}

TEST_CASE("open() passes extra arguments to the format's constructor")
{
    MemFile file(make_image(16));

    auto media = Media::open<ProbeMedia>("cart.bin", &file, 42);
    REQUIRE(media != nullptr);
    CHECK(media->extra == 42);
}

TEST_CASE("open() returns nullptr when attach() refuses the file")
{
    MemFile file(make_image(16));

    CHECK(Media::open<ProbeMedia>("cart.bin", &file, 0, false) == nullptr);
}

TEST_CASE("matchExtension() returns the list's entry, whatever the path's case")
{
    CHECK(Media::matchExtension("cart.BIN", {".rom", ".bin"}) == ".bin");
    CHECK(Media::matchExtension("Cart.Rom", {".rom", ".bin"}) == ".rom");
    CHECK(Media::matchExtension("tnfs://host/dir/cart.bin?mode=r#top", {".bin"}) == ".bin");
    CHECK(Media::matchExtension("dir.bin/cart", {".bin"}) == std::nullopt);
    CHECK(Media::matchExtension("cart", {".bin"}) == std::nullopt);
    CHECK(Media::matchExtension("cart.bin.gz", {".bin"}) == std::nullopt);
    CHECK(Media::matchExtension("", {".bin"}) == std::nullopt);
}

TEST_CASE("findSibling() tries the extension as given, then in the other case")
{
    fujiHost host;

    host_files.reset();
    add_host_file("dir/cart.cfg", "");
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".cfg") == "dir/cart.cfg");
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".CFG") == "dir/cart.cfg");
    CHECK(Media::findSibling(&host, "dir/cart", ".cfg") == "dir/cart.cfg");
    CHECK(Media::findSibling(&host, "dir/other.bin", ".cfg") == std::nullopt);

    host_files.reset();
    add_host_file("dir/cart.CFG", "");
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".cfg") == "dir/cart.CFG");

    host_files.reset();
    add_host_file("dir/cart.Cfg", "");
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".cfg") == std::nullopt);
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".CFG") == std::nullopt);
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".Cfg") == "dir/cart.Cfg");

    host_files.reset();
    add_host_file("dir/cart.cfg", "");
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".Cfg") == std::nullopt);
    CHECK(Media::findSibling(nullptr, "dir/cart.bin", ".cfg") == std::nullopt);
}

TEST_CASE("findSibling() takes the host's prefix off before asking the host")
{
    fujiHost host;

    host_files.reset();
    add_host_file("dir/cart.cfg", "");

    host_files.prefix = "/base";
    CHECK(Media::findSibling(&host, "/base/dir/cart.bin", ".cfg") == "dir/cart.cfg");

    host_files.prefix = "/base/";
    CHECK(Media::findSibling(&host, "/base/dir/cart.bin", ".cfg") == "dir/cart.cfg");

    host_files.prefix = "/base";
    CHECK(Media::findSibling(&host, "dir/cart.bin", ".cfg") == "dir/cart.cfg");
    CHECK(Media::findSibling(&host, "/other/dir/cart.bin", ".cfg") == std::nullopt);
}

TEST_CASE("contentsOfHostFile() returns the whole file and closes it")
{
    fujiHost host;

    host_files.reset();
    add_host_file("dir/cart.cfg", "mapper=test\n");
    auto contents = Media::contentsOfHostFile(&host, "dir/cart.cfg", 64);
    REQUIRE(contents.has_value());
    CHECK(std::string(contents->begin(), contents->end()) == "mapper=test\n");
    REQUIRE(host_files.opened.size() == 1);
    CHECK(host_files.opened[0]->closes == 1);

    host_files.reset();
    add_host_file("dir/empty.cfg", "");
    contents = Media::contentsOfHostFile(&host, "dir/empty.cfg", 64);
    REQUIRE(contents.has_value());
    CHECK(contents->empty());

    host_files.reset();
    CHECK(Media::contentsOfHostFile(&host, "dir/missing.cfg", 64) == std::nullopt);

    add_host_file("dir/cart.cfg", "mapper=test\n");
    CHECK(Media::contentsOfHostFile(nullptr, "dir/cart.cfg", 64) == std::nullopt);
}

TEST_CASE("contentsOfHostFile() refuses a file over maxSize without reading it")
{
    fujiHost host;

    host_files.reset();
    add_host_file("dir/cart.cfg", "mapper=test\n");
    CHECK(Media::contentsOfHostFile(&host, "dir/cart.cfg", 11) == std::nullopt);
    REQUIRE(host_files.opened.size() == 1);
    CHECK(host_files.opened[0]->closes == 1);

    host_files.reset();
    add_host_file("dir/cart.cfg", "mapper=test\n");
    CHECK(Media::contentsOfHostFile(&host, "dir/cart.cfg", 12).has_value());
}
