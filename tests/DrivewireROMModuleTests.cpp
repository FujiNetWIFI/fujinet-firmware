#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "fujiHost.h"
#include "romMountStubs.h"
#include "romModule.h"

namespace
{

constexpr size_t PAGE = PMD85ROM::PAGE_SIZE;
const char *const MODULE_PATH = "game.mrm";

using PageStatus = drivewireROMModule::PageStatus;

std::vector<uint8_t> page_of(const std::vector<uint8_t> &image, size_t number)
{
    std::vector<uint8_t> page(PAGE, 0xFF);
    size_t start = number * PAGE;
    size_t count = std::min(PAGE, image.size() - start);
    std::copy(image.begin() + start, image.begin() + start + count, page.begin());
    return page;
}

std::vector<uint8_t> loaded(drivewireROMModule &module)
{
    return std::vector<uint8_t>(module.page(), module.page() + PAGE);
}

// The module as drivewireFuji mounts it: the host serves path, the module's Formats opens it.
std::unique_ptr<drivewireROMModule> mount(fujiHost &host, const std::vector<uint8_t> &image,
                                          const char *path = MODULE_PATH)
{
    host_files.reset();
    host_files.files[path] = image;
    char fullpath[64];
    fnFile *file = host.fnfile_open(path, fullpath, sizeof(fullpath), "rb");
    auto rom = drivewireROMModule::Formats::open<ROMMedia>(path, file, &host);
    REQUIRE(rom != nullptr);
    auto module = drivewireROMModule::create(std::move(rom), host, path);
    REQUIRE(module != nullptr);
    return module;
}

} // namespace

TEST_CASE("a module serves its pages in order, padding the last with $FF")
{
    fujiHost host;
    auto image = make_image(2 * PAGE + 300);
    auto module = mount(host, image);

    for (uint32_t number = 0; number < 3; number++)
    {
        REQUIRE(module->load(number) == PageStatus::OK);
        CHECK(loaded(*module) == page_of(image, number));
    }
}

TEST_CASE("a page past the end is reported, and the module still serves the rest")
{
    fujiHost host;
    auto image = make_image(PAGE + 1);
    auto module = mount(host, image);

    CHECK(module->load(2) == PageStatus::PAST_END);
    CHECK(module->load(255) == PageStatus::PAST_END);
    REQUIRE(module->load(1) == PageStatus::OK);
    CHECK(loaded(*module) == page_of(image, 1));
}

TEST_CASE("a page read again is served without opening the file again")
{
    fujiHost host;
    auto image = make_image(2 * PAGE);
    auto module = mount(host, image);

    REQUIRE(module->load(1) == PageStatus::OK);
    size_t opened = host_files.opened.size();
    REQUIRE(module->load(1) == PageStatus::OK);
    CHECK(host_files.opened.size() == opened);
    CHECK(loaded(*module) == page_of(image, 1));
}

TEST_CASE("pages are served in any order")
{
    fujiHost host;
    auto image = make_image(4 * PAGE - 5);
    auto module = mount(host, image);

    for (uint32_t number : {2u, 0u, 3u, 1u, 3u, 3u, 0u})
    {
        REQUIRE(module->load(number) == PageStatus::OK);
        CHECK(loaded(*module) == page_of(image, number));
    }
}

TEST_CASE("a read that comes up short is an error, and the module recovers")
{
    fujiHost host;
    auto image = make_image(3 * PAGE);
    auto module = mount(host, image);
    REQUIRE(module->load(0) == PageStatus::OK);

    // The file shrinks under the module
    host_files.files[MODULE_PATH].resize(PAGE + 10);
    CHECK(module->load(2) == PageStatus::READ_ERROR);
    CHECK(module->load(2) == PageStatus::READ_ERROR);

    host_files.files[MODULE_PATH] = image;
    REQUIRE(module->load(2) == PageStatus::OK);
    CHECK(loaded(*module) == page_of(image, 2));
}

TEST_CASE("the module closes its file when it goes away")
{
    fujiHost host;
    auto image = make_image(2 * PAGE);
    auto module = mount(host, image);
    REQUIRE(module->load(1) == PageStatus::OK);
    REQUIRE(module->load(0) == PageStatus::OK);

    module.reset();
    for (const auto &handle : host_files.opened)
        CHECK(handle->closes == 1);
}

TEST_CASE("PMD85ROM takes .rmm and .mrm up to 256 pages")
{
    fujiHost host;
    char fullpath[64];

    auto open_as = [&](const char *path, size_t length) {
        host_files.reset();
        host_files.files[path] = make_image(length);
        fnFile *file = host.fnfile_open(path, fullpath, sizeof(fullpath), "rb");
        return Media::open<PMD85ROM>(path, file, &host);
    };

    CHECK(open_as("a.rmm", PAGE) != nullptr);
    CHECK(open_as("A.MRM", 256 * PAGE) != nullptr);
    CHECK(open_as("a.mrm", 256 * PAGE + 1) == nullptr);
    CHECK(open_as("a.mrm", 0) == nullptr);
    CHECK(open_as("a.rom", PAGE) == nullptr);
}
