#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "IMDFixture.h"
#include "IMDDisk.h"
#include "fujiHost.h"

// media.cpp's sibling-file helpers go through the host; these tests never reach them.
const char *fujiHost::get_prefix() { return ""; }
bool fujiHost::file_exists(const char *) { return false; }
fnFile *fujiHost::fnfile_open(const char *, char *, int, const char *) { return nullptr; }
file_size_t fujiHost::file_size(fnFile *) { return file_size_t(-1); }

namespace
{

// fnFile is std::FILE here. A name of its own so a parallel ctest run cannot
// collide with the other IMD tests.
#define IMD_DISK_TEST_FILE "imddisk_test.tmp"

FILE *image_file(const std::vector<uint8_t> &bytes)
{
    FILE *file = std::fopen(IMD_DISK_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    REQUIRE(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    return file;
}

std::unique_ptr<IMDDisk> open_imd(const char *path, FILE *file, bool readOnly = true)
{
    return Media::open<IMDDisk>(path, file, nullptr, readOnly);
}

void remove_image(FILE *file)
{
    std::fclose(file);
    std::remove(IMD_DISK_TEST_FILE);
}

// A 2 cylinder x 2 head, 4 sector, 128-byte MFM image. Fill byte encodes the
// track so a read can prove which block it landed on.
IMD two_by_two()
{
    IMD image;
    image.header();
    image.simple_track(3, 0, 0, 4, 0xA0);
    image.simple_track(3, 0, 1, 4, 0xA1);
    image.simple_track(3, 1, 0, 4, 0xB0);
    image.simple_track(3, 1, 1, 4, 0xB1);
    return image;
}

bool filled_with(const ByteBuffer &buffer, uint8_t value)
{
    return !buffer.empty()
           && std::all_of(buffer.begin(), buffer.end(),
                          [value](uint8_t byte) { return byte == value; });
}

} // namespace

TEST_CASE("an IMD reports its geometry from the track headers")
{
    FILE *file = image_file(two_by_two().out);
    auto media = open_imd("disk.imd", file);
    REQUIRE(media != nullptr);
    CHECK(media->blockCount() == 16);
    CHECK(media->cylinderCount() == 2);
    CHECK(media->sideCount() == 2);
    remove_image(file);
}

TEST_CASE("an IMD track's geometry and encoding come from its header")
{
    IMD image;
    image.header();
    image.simple_track(0, 0, 0, 26, 0x11); // 500 kbps FM
    image.simple_track(3, 1, 0, 8, 0x22);  // 500 kbps MFM
    image.thdr(3, 2, 0, 2, 2);             // size code 2 = 512
    image.map({1, 2});
    image.comp(0x02, 0x33);
    image.comp(0x02, 0x44);
    FILE *file = image_file(image.out);
    auto media = open_imd("disk.imd", file);
    REQUIRE(media != nullptr);

    TrackGeometry geometry = media->trackGeometry(0, 0);
    CHECK(geometry.sectorCount == 26);
    CHECK(geometry.sectorSize == 128);
    CHECK(geometry.encoding == TrackEncoding::FM);

    geometry = media->trackGeometry(1, 0);
    CHECK(geometry.sectorCount == 8);
    CHECK(geometry.encoding == TrackEncoding::MFM);

    geometry = media->trackGeometry(2, 0);
    CHECK(geometry.sectorCount == 2);
    CHECK(geometry.sectorSize == 512);

    CHECK(media->trackGeometry(3, 0).sectorCount == 0);
    CHECK(media->trackGeometry(0, 1).sectorCount == 0);
    remove_image(file);
}

TEST_CASE("IMD blocks are its LBAs, in track order")
{
    FILE *file = image_file(two_by_two().out);
    auto media = open_imd("disk.imd", file);
    REQUIRE(media != nullptr);

    ByteBuffer buffer;
    REQUIRE(media->readBlock(0, buffer).is_success());
    CHECK(buffer.size() == 128);
    CHECK(filled_with(buffer, 0xA0));
    REQUIRE(media->readBlock(4, buffer).is_success());
    CHECK(filled_with(buffer, 0xA1));
    REQUIRE(media->readBlock(8, buffer).is_success());
    CHECK(filled_with(buffer, 0xB0));
    REQUIRE(media->readBlock(15, buffer).is_success());
    CHECK(filled_with(buffer, 0xB1));
    CHECK(media->readBlock(16, buffer).is_error());
    remove_image(file);
}

TEST_CASE("each IMD block has its own sector's size")
{
    // 128-byte track 0, then a 512-byte track: a mixed density CP/M disk
    IMD image;
    image.header();
    image.simple_track(3, 0, 0, 2, 0x77);
    image.thdr(3, 1, 0, 2, 2); // size code 2 = 512
    image.map({1, 2});
    image.comp(0x02, 0x33);
    image.comp(0x02, 0x44);
    FILE *file = image_file(image.out);
    auto media = open_imd("disk.imd", file);
    REQUIRE(media != nullptr);

    CHECK(media->blockSize(1) == 128);
    CHECK(media->blockSize(2) == 512);
    CHECK(media->blockSize(4) == 0);

    ByteBuffer buffer;
    REQUIRE(media->readBlock(2, buffer).is_success());
    CHECK(buffer.size() == 512);
    CHECK(filled_with(buffer, 0x33));
    remove_image(file);
}

TEST_CASE("an IMD is found by its contents unless the name says raw image")
{
    std::vector<uint8_t> bytes = two_by_two().out;
    for (const char *path : {"disk.imd", "DISK.IMD", "DISK1", "cpm.xyz"})
    {
        CAPTURE(path);
        FILE *file = image_file(bytes);
        CHECK(open_imd(path, file) != nullptr);
        remove_image(file);
    }
    for (const char *path : {"cpm.img", "cpm.dsk"})
    {
        CAPTURE(path);
        FILE *file = image_file(bytes);
        CHECK(open_imd(path, file) == nullptr);
        remove_image(file);
    }

    FILE *raw = image_file(std::vector<uint8_t>(16 * 128, 0xE5));
    CHECK(open_imd("DISK1", raw) == nullptr);
    CHECK(open_imd("disk.imd", raw) == nullptr);
    remove_image(raw);
}

TEST_CASE("an IMD with no tracks is refused")
{
    IMD image;
    image.header();
    FILE *file = image_file(image.out);
    CHECK(open_imd("disk.imd", file) == nullptr);
    remove_image(file);
}

TEST_CASE("a damaged IMD record reads back its data; an unavailable one fails")
{
    IMD image;
    image.header();
    image.thdr(3, 0, 0, 3, 0);
    image.map({1, 2, 3});
    image.comp(0x06, 0xDE); // compressed + data error
    image.comp(0x04, 0xAD); // deleted address mark
    image.unavailable();
    FILE *file = image_file(image.out);
    auto media = open_imd("disk.imd", file, false);
    REQUIRE(media != nullptr);

    ByteBuffer buffer;
    REQUIRE(media->readBlock(0, buffer).is_success());
    CHECK(filled_with(buffer, 0xDE));
    REQUIRE(media->readBlock(1, buffer).is_success());
    CHECK(filled_with(buffer, 0xAD));
    CHECK(media->readBlock(2, buffer).is_error());
    CHECK(media->writeBlock(2, ByteBuffer(128, 0x5A)).is_error());
    remove_image(file);
}

TEST_CASE("a writable IMD takes a sector in place; a read-only one refuses it")
{
    FILE *file = image_file(two_by_two().out);

    auto readOnly = open_imd("disk.imd", file);
    REQUIRE(readOnly != nullptr);
    CHECK(readOnly->isReadOnly());
    CHECK(readOnly->writeBlock(5, ByteBuffer(128, 0x5A)).is_error());

    auto writable = open_imd("disk.imd", file, false);
    REQUIRE(writable != nullptr);
    CHECK(writable->writeBlock(5, ByteBuffer(127, 0x5A)).is_error());
    REQUIRE(writable->writeBlock(5, ByteBuffer(128, 0x5A)).is_success());

    ByteBuffer buffer;
    REQUIRE(writable->readBlock(5, buffer).is_success());
    CHECK(filled_with(buffer, 0x5A));
    REQUIRE(writable->readBlock(4, buffer).is_success());
    CHECK(filled_with(buffer, 0xA1));
    remove_image(file);
}

TEST_CASE("an IBM-3740 image from create() opens with E5 through the directory track")
{
    FILE *file = std::fopen(IMD_DISK_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    REQUIRE(IMDDisk::create(file, 128, 77 * 26).is_success());

    auto media = open_imd("new.imd", file, false);
    REQUIRE(media != nullptr);
    CHECK(media->blockCount() == 77 * 26);
    CHECK(media->cylinderCount() == 77);
    CHECK(media->sideCount() == 1);

    ByteBuffer buffer;
    REQUIRE(media->readBlock(3 * 26 - 1, buffer).is_success());
    CHECK(filled_with(buffer, 0xE5));
    REQUIRE(media->readBlock(3 * 26, buffer).is_success());
    CHECK(filled_with(buffer, 0x00));
    REQUIRE(media->writeBlock(77 * 26 - 1, ByteBuffer(128, 0x42)).is_success());
    REQUIRE(media->readBlock(77 * 26 - 1, buffer).is_success());
    CHECK(filled_with(buffer, 0x42));
    remove_image(file);
}

TEST_CASE("create() refuses any geometry but IBM-3740 and writes nothing")
{
    struct
    {
        uint16_t sectorSize;
        uint32_t numSectors;
    } sizes[] = {{128, 720}, {256, 77 * 26}, {512, 77 * 26}, {128, 0}, {0, 77 * 26}};
    for (auto size : sizes)
    {
        CAPTURE(size.sectorSize);
        CAPTURE(size.numSectors);
        FILE *file = std::fopen(IMD_DISK_TEST_FILE, "w+b");
        REQUIRE(file != nullptr);
        CHECK(IMDDisk::create(file, size.sectorSize, size.numSectors).is_error());
        std::fseek(file, 0, SEEK_END);
        CHECK(std::ftell(file) == 0);
        remove_image(file);
    }
}
