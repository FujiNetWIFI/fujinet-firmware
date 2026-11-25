#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <vector>

#include "JV1Disk.h"
#include "MediaFormats.h"
#include "fujiHost.h"

// media.cpp's sibling-file helpers go through the host; these tests never reach them.
const char *fujiHost::get_prefix() { return ""; }
bool fujiHost::file_exists(const char *) { return false; }
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }
file_size_t fujiHost::file_size(fnFile *) { return file_size_t(-1); }

namespace
{

// fnFile is std::FILE here; a name of its own so a parallel ctest run cannot collide.
#define JV1_TEST_FILE "jv1disk_test.tmp"

uint8_t pattern(uint32_t sector, uint32_t idx)
{
    return (uint8_t)(sector * 7 + idx);
}

FILE *image_file(uint32_t tracks)
{
    FILE *file = std::fopen(JV1_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    std::vector<uint8_t> sector(JV1_SECTOR_SIZE);
    for (uint32_t lba = 0; lba < tracks * JV1_SECTORS_TRACK; lba++)
    {
        for (uint32_t idx = 0; idx < JV1_SECTOR_SIZE; idx++)
            sector[idx] = pattern(lba, idx);
        REQUIRE(std::fwrite(sector.data(), 1, sector.size(), file) == sector.size());
    }
    return file;
}

std::unique_ptr<JV1Disk> open_jv1(const char *path, FILE *file, bool readOnly = false)
{
    return Media::open<JV1Disk>(path, file, nullptr, readOnly);
}

void remove_image(FILE *file)
{
    std::fclose(file);
    std::remove(JV1_TEST_FILE);
}

} // namespace

TEST_CASE("a .jv1 opens at any whole number of tracks, a .dsk only at 35, 40 or 80")
{
    for (uint32_t tracks : {35u, 40u, 80u})
    {
        CAPTURE(tracks);
        FILE *file = image_file(tracks);
        CHECK(open_jv1("trsdos.dsk", file) != nullptr);
        CHECK(open_jv1("TRSDOS.DSK", file) != nullptr);
        CHECK(open_jv1("trsdos.jv1", file) != nullptr);
        CHECK(open_jv1("trsdos.img", file) == nullptr);
        remove_image(file);
    }
    for (uint32_t tracks : {1u, 12u, 77u})
    {
        CAPTURE(tracks);
        FILE *file = image_file(tracks);
        CHECK(open_jv1("config.jv1", file) != nullptr);
        CHECK(open_jv1("config.dsk", file) == nullptr);
        remove_image(file);
    }
    FILE *file = image_file(97);
    CHECK(open_jv1("huge.jv1", file) == nullptr);
    remove_image(file);

    // Not a whole number of tracks, or empty.
    file = std::fopen(JV1_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    CHECK(open_jv1("empty.jv1", file) == nullptr);
    REQUIRE(std::fwrite("x", 1, 1, file) == 1);
    CHECK(open_jv1("odd.jv1", file) == nullptr);
    remove_image(file);
}

TEST_CASE("a JV1 image is one side of ten 256-byte FM sectors a track")
{
    FILE *file = image_file(40);
    auto media = open_jv1("newdos.dsk", file);
    REQUIRE(media != nullptr);
    CHECK(media->cylinderCount() == 40);
    CHECK(media->sideCount() == 1);
    CHECK(media->blockCount() == 400);

    TrackGeometry geometry = media->trackGeometry(39, 0);
    CHECK(geometry.sectorCount == JV1_SECTORS_TRACK);
    CHECK(geometry.sectorSize == JV1_SECTOR_SIZE);
    CHECK(geometry.encoding == TrackEncoding::FM);
    CHECK(media->trackGeometry(40, 0).sectorCount == 0);
    CHECK(media->trackGeometry(0, 1).sectorCount == 0);
    remove_image(file);
}

TEST_CASE("JV1 block N is 256 bytes at N * 256")
{
    FILE *file = image_file(35);
    auto media = open_jv1("ldos.dsk", file, true);
    REQUIRE(media != nullptr);

    for (uint32_t sector : {0u, 170u, 349u})
    {
        CAPTURE(sector);
        ByteBuffer buffer;
        REQUIRE(media->readBlock(sector, buffer).is_success());
        REQUIRE(buffer.size() == JV1_SECTOR_SIZE);
        CHECK(buffer[0] == pattern(sector, 0));
        CHECK(buffer[255] == pattern(sector, 255));
    }

    ByteBuffer buffer;
    CHECK(media->readBlock(350, buffer).is_error());
    CHECK(media->blockSize(350) == 0);
    remove_image(file);
}

TEST_CASE("a JV1 write lands in place and is flushed; a read-only image refuses it")
{
    FILE *file = image_file(35);
    auto media = open_jv1("work.jv1", file);
    REQUIRE(media != nullptr);

    REQUIRE(media->writeBlock(171, ByteBuffer(JV1_SECTOR_SIZE, 0xE5)).is_success());
    CHECK(media->writeBlock(350, ByteBuffer(JV1_SECTOR_SIZE, 0xE5)).is_error());
    CHECK(media->writeBlock(172, ByteBuffer(128, 0xE5)).is_error());

    FILE *check = std::fopen(JV1_TEST_FILE, "rb");
    REQUIRE(check != nullptr);
    std::vector<uint8_t> sectors(173 * JV1_SECTOR_SIZE);
    REQUIRE(std::fread(sectors.data(), 1, sectors.size(), check) == sectors.size());
    std::fclose(check);
    CHECK(sectors[171 * JV1_SECTOR_SIZE] == 0xE5);
    CHECK(sectors[172 * JV1_SECTOR_SIZE - 1] == 0xE5);
    CHECK(sectors[170 * JV1_SECTOR_SIZE] == pattern(170, 0));
    CHECK(sectors[172 * JV1_SECTOR_SIZE] == pattern(172, 0));
    media.reset();

    media = open_jv1("work.jv1", file, true);
    REQUIRE(media != nullptr);
    CHECK(media->isReadOnly());
    CHECK(media->writeBlock(0, ByteBuffer(JV1_SECTOR_SIZE, 0xE5)).is_error());
    remove_image(file);
}

TEST_CASE("only a .jv1 lists as JV1, since a .dsk may be another format")
{
    using Formats = MediaFormats<0, MediaFormat<JV1Disk, 1>>;
    CHECK(Formats::entryType("trsdos.jv1") == 1);
    CHECK(Formats::entryType("TRSDOS.JV1") == 1);
    CHECK(Formats::entryType("trsdos.dsk") == 0);
}
