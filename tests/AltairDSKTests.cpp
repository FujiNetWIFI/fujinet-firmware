#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "AltairDSK.h"
#include "fujiHost.h"

// media.cpp's sibling-file helpers go through the host; these tests never reach them.
const char *fujiHost::get_prefix() { return ""; }
bool fujiHost::file_exists(const char *) { return false; }
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }
file_size_t fujiHost::file_size(fnFile *) { return file_size_t(-1); }

namespace
{

// fnFile is std::FILE here. A name of its own so a parallel ctest run cannot
// collide with the other Altair tests.
#define ALTAIR_DSK_TEST_FILE "altairdsk_test.tmp"

constexpr size_t ALTAIR_8INCH = 337568;
constexpr size_t ALTAIR_8INCH_PADDED = 337664;
constexpr size_t ALTAIR_MINIDISK = 76720;
constexpr size_t ALTAIR_MINIDISK_PADDED = 76800;
constexpr size_t ALTAIR_8MB = 8978432;

uint8_t pattern(uint32_t sector, uint32_t idx)
{
    return (uint8_t)(sector * 7 + idx);
}

// size bytes, whose first sectors carry pattern(); the rest is a sparse zero tail.
FILE *image_file(size_t size, uint32_t sectors)
{
    FILE *file = std::fopen(ALTAIR_DSK_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    std::vector<uint8_t> sector(ALTAIR_DSK_SECTOR_SIZE);
    for (uint32_t lba = 0; lba < sectors; lba++)
    {
        for (uint32_t idx = 0; idx < ALTAIR_DSK_SECTOR_SIZE; idx++)
            sector[idx] = pattern(lba, idx);
        REQUIRE(std::fwrite(sector.data(), 1, sector.size(), file) == sector.size());
    }
    if (size > sectors * ALTAIR_DSK_SECTOR_SIZE)
    {
        REQUIRE(std::fseek(file, size - 1, SEEK_SET) == 0);
        REQUIRE(std::fputc(0, file) == 0);
    }
    return file;
}

long file_size(FILE *file)
{
    std::fseek(file, 0, SEEK_END);
    return std::ftell(file);
}

std::unique_ptr<AltairDSK> open_altair(const char *path, FILE *file, bool readOnly = false)
{
    return Media::open<AltairDSK>(path, file, nullptr, readOnly);
}

void remove_image(FILE *file)
{
    std::fclose(file);
    std::remove(ALTAIR_DSK_TEST_FILE);
}

} // namespace

TEST_CASE("only a .dsk of an FDC+ size opens as Altair")
{
    for (size_t size :
         {ALTAIR_8INCH, ALTAIR_8INCH_PADDED, ALTAIR_MINIDISK, ALTAIR_MINIDISK_PADDED})
    {
        CAPTURE(size);
        FILE *file = image_file(size, 0);
        CHECK(open_altair("cpm.dsk", file) != nullptr);
        CHECK(open_altair("CPM.DSK", file) != nullptr);
        CHECK(open_altair("cpm.img", file) == nullptr);
        remove_image(file);
    }

    FILE *file = image_file(368640, 0);
    CHECK(open_altair("pc.dsk", file) == nullptr);
    remove_image(file);
}

TEST_CASE("an Altair image's size fixes its geometry, one side of FM tracks")
{
    struct
    {
        size_t size;
        uint16_t cylinders;
        uint8_t sectorsPerTrack;
    } images[] = {{ALTAIR_8INCH, 77, 32},
                  {ALTAIR_8INCH_PADDED, 77, 32},
                  {ALTAIR_MINIDISK, 35, 16},
                  {ALTAIR_MINIDISK_PADDED, 35, 16},
                  {ALTAIR_8MB, 2048, 32}};
    for (auto image : images)
    {
        CAPTURE(image.size);
        FILE *file = image_file(image.size, 0);
        auto media = open_altair("cpm.dsk", file);
        REQUIRE(media != nullptr);
        CHECK(media->cylinderCount() == image.cylinders);
        CHECK(media->sideCount() == 1);
        CHECK(media->blockCount() == (uint32_t)image.cylinders * image.sectorsPerTrack);

        TrackGeometry geometry = media->trackGeometry(image.cylinders - 1, 0);
        CHECK(geometry.sectorCount == image.sectorsPerTrack);
        CHECK(geometry.sectorSize == ALTAIR_DSK_SECTOR_SIZE);
        CHECK(geometry.encoding == TrackEncoding::FM);
        CHECK(media->trackGeometry(image.cylinders, 0).sectorCount == 0);
        CHECK(media->trackGeometry(0, 1).sectorCount == 0);
        remove_image(file);
    }
}

TEST_CASE("Altair block N is 137 bytes at N * 137, and the padding is never read")
{
    FILE *file = image_file(ALTAIR_8INCH_PADDED, 77 * 32);
    auto media = open_altair("cpm.dsk", file, true);
    REQUIRE(media != nullptr);

    for (uint32_t sector : {0u, 33u, 77u * 32 - 1})
    {
        CAPTURE(sector);
        ByteBuffer buffer;
        REQUIRE(media->readBlock(sector, buffer).is_success());
        REQUIRE(buffer.size() == ALTAIR_DSK_SECTOR_SIZE);
        CHECK(buffer[0] == pattern(sector, 0));
        CHECK(buffer[136] == pattern(sector, 136));
    }

    ByteBuffer buffer;
    CHECK(media->readBlock(77 * 32, buffer).is_error());
    CHECK(media->blockSize(77 * 32) == 0);
    remove_image(file);
}

TEST_CASE("an Altair write lands in place, is flushed, and never grows the file")
{
    FILE *file = image_file(ALTAIR_8INCH, 77 * 32);
    auto media = open_altair("cpm.dsk", file);
    REQUIRE(media != nullptr);

    REQUIRE(media->writeBlock(40, ByteBuffer(ALTAIR_DSK_SECTOR_SIZE, 0xC3)).is_success());
    CHECK(media->writeBlock(77 * 32, ByteBuffer(ALTAIR_DSK_SECTOR_SIZE, 0xC3)).is_error());
    CHECK(media->writeBlock(41, ByteBuffer(128, 0xC3)).is_error());

    FILE *check = std::fopen(ALTAIR_DSK_TEST_FILE, "rb");
    REQUIRE(check != nullptr);
    std::vector<uint8_t> sectors(42 * ALTAIR_DSK_SECTOR_SIZE);
    REQUIRE(std::fread(sectors.data(), 1, sectors.size(), check) == sectors.size());
    std::fclose(check);
    CHECK(sectors[40 * ALTAIR_DSK_SECTOR_SIZE] == 0xC3);
    CHECK(sectors[41 * ALTAIR_DSK_SECTOR_SIZE - 1] == 0xC3);
    CHECK(sectors[39 * ALTAIR_DSK_SECTOR_SIZE] == pattern(39, 0));
    CHECK(sectors[41 * ALTAIR_DSK_SECTOR_SIZE] == pattern(41, 0));

    CHECK(file_size(file) == (long)ALTAIR_8INCH);
    remove_image(file);
}

TEST_CASE("a read-only Altair image refuses writes")
{
    FILE *file = image_file(ALTAIR_MINIDISK, 35 * 16);
    auto media = open_altair("cpm.dsk", file, true);
    REQUIRE(media != nullptr);
    CHECK(media->isReadOnly());
    CHECK(media->writeBlock(0, ByteBuffer(ALTAIR_DSK_SECTOR_SIZE, 0xC3)).is_error());

    ByteBuffer buffer;
    REQUIRE(media->readBlock(0, buffer).is_success());
    CHECK(buffer[0] == pattern(0, 0));
    remove_image(file);
}
