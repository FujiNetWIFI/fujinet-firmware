#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "diskTypeImg.h"

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
#define IMG_TEST_FILE "mediatypeimg_test.tmp"

long file_size(fnFile *fp)
{
    fseek(fp, 0, SEEK_END);
    return ftell(fp);
}

std::vector<uint8_t> file_bytes(fnFile *fp)
{
    std::vector<uint8_t> b(file_size(fp));
    rewind(fp);
    REQUIRE(fread(b.data(), 1, b.size(), fp) == b.size());
    return b;
}

} // namespace

TEST_CASE("create() writes numSectors * sectorSize zero bytes")
{
    struct { uint16_t sectorSize; uint32_t numSectors; } sizes[] = {
        {512, 720}, {512, 2880}, {128, 720}, {256, 1}, {512, 1},
    };
    for (auto s : sizes)
    {
        CAPTURE(s.sectorSize);
        CAPTURE(s.numSectors);
        fnFile *fp = fopen(IMG_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        CHECK(MediaTypeImg::create(fp, s.sectorSize, s.numSectors).is_success());
        auto bytes = file_bytes(fp);
        CHECK(bytes.size() == (size_t)s.sectorSize * s.numSectors);
        CHECK(std::count(bytes.begin(), bytes.end(), 0) == (long)bytes.size());
        fclose(fp);
        remove(IMG_TEST_FILE);
    }
}

TEST_CASE("an image create() made mounts and reads to its last sector")
{
    fnFile *fp = fopen(IMG_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    REQUIRE(MediaTypeImg::create(fp, 512, 720).is_success());
    rewind(fp);

    MediaTypeImg media;
    CHECK(media.mount(fp, 512 * 720) == MEDIATYPE_IMG);
    uint32_t n = 0;
    CHECK(media.read(0, &n).is_success());
    CHECK(n == 512);
    CHECK(media.read(719, &n).is_success());
    CHECK(media.read(720, &n).is_error());
    media.unmount(); // closes fp
    remove(IMG_TEST_FILE);
}

TEST_CASE("create() refuses an empty image")
{
    fnFile *fp = fopen(IMG_TEST_FILE, "w+b");
    REQUIRE(fp != nullptr);
    CHECK(MediaTypeImg::create(fp, 512, 0).is_error());
    CHECK(MediaTypeImg::create(fp, 0, 720).is_error());
    CHECK(file_size(fp) == 0);
    fclose(fp);
    remove(IMG_TEST_FILE);
}
