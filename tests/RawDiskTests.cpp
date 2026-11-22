#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

#include "RawDisk.h"

namespace
{

// Not tmpfile(): under MSYS2 that creates the file in the drive root.
#define RAW_DISK_TEST_FILE "rawdisk_test.tmp"

// Every sector holds different bytes, so a read from the wrong place shows.
std::vector<uint8_t> numbered_sectors(size_t count)
{
    std::vector<uint8_t> image(count * RAW_DISK_SECTOR_SIZE);
    for (size_t idx = 0; idx < image.size(); idx++)
        image[idx] = (uint8_t)(idx / RAW_DISK_SECTOR_SIZE * 7 + idx % RAW_DISK_SECTOR_SIZE);
    return image;
}

FILE *image_file(const std::vector<uint8_t> &image)
{
    FILE *file = std::fopen(RAW_DISK_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    REQUIRE(std::fwrite(image.data(), 1, image.size(), file) == image.size());
    return file;
}

// Read through a handle of its own, so it only sees what was flushed.
std::vector<uint8_t> flushed_contents()
{
    FILE *file = std::fopen(RAW_DISK_TEST_FILE, "rb");
    REQUIRE(file != nullptr);
    std::fseek(file, 0, SEEK_END);
    std::vector<uint8_t> contents(std::ftell(file));
    std::fseek(file, 0, SEEK_SET);
    REQUIRE(std::fread(contents.data(), 1, contents.size(), file) == contents.size());
    std::fclose(file);
    return contents;
}

std::unique_ptr<RawDisk> open_raw(const char *path, FILE *file, bool readOnly = false)
{
    return Media::open<RawDisk>(path, file, nullptr, readOnly);
}

void remove_image(FILE *file)
{
    std::fclose(file);
    std::remove(RAW_DISK_TEST_FILE);
}

} // namespace

TEST_CASE("a raw disk opens whatever the file is called")
{
    FILE *file = image_file(numbered_sectors(4));
    for (const char *path : {"disk.img", "GAME.XEX", "cpm.dsk", "DISK1", "notes.txt"})
        CHECK(open_raw(path, file) != nullptr);
    remove_image(file);
}

TEST_CASE("a raw disk has one block per whole 512-byte sector")
{
    auto image = numbered_sectors(5);
    image.resize(image.size() + 100);
    FILE *file = image_file(image);
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);
    CHECK(media->blockCount() == 5);
    CHECK(media->blockSize(0) == RAW_DISK_SECTOR_SIZE);
    CHECK(media->blockSize(4) == RAW_DISK_SECTOR_SIZE);
    CHECK(media->blockSize(5) == 0);
    remove_image(file);
}

TEST_CASE("a raw disk's geometry comes from a FAT boot sector that matches its size")
{
    // A 360K boot sector, as data/BUILD_RS232/autorun.img has
    std::vector<uint8_t> image(720 * RAW_DISK_SECTOR_SIZE, 0);
    const uint8_t boot[] = {0xEB, 0x3C, 0x90, 'M', 'T', 'O', 'O', '4', '0', '4', '8',
                            0x00, 0x02, 2, 1, 0, 2, 112, 0, 0xD0, 0x02, 0xFD, 2, 0, 9, 0, 2, 0};
    std::copy(std::begin(boot), std::end(boot), image.begin());
    FILE *file = image_file(image);
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);
    CHECK(media->cylinderCount() == 40);
    CHECK(media->sideCount() == 2);
    TrackGeometry geometry = media->trackGeometry(39, 1);
    CHECK(geometry.sectorCount == 9);
    CHECK(geometry.sectorSize == RAW_DISK_SECTOR_SIZE);
    CHECK(geometry.encoding == TrackEncoding::MFM);
    CHECK(media->trackGeometry(40, 0).sectorCount == 0);
    CHECK(media->trackGeometry(0, 2).sectorCount == 0);
    remove_image(file);
}

TEST_CASE("a raw disk without a usable boot sector takes a PC floppy's geometry by size")
{
    struct
    {
        uint32_t sectors;
        uint16_t cylinders;
        uint8_t sides;
        uint8_t sectorsPerTrack;
    } floppies[] = {{320, 40, 1, 8},   {360, 40, 1, 9},   {640, 40, 2, 8},
                    {720, 40, 2, 9},   {1440, 80, 2, 9},  {2400, 80, 2, 15},
                    {2880, 80, 2, 18}, {5760, 80, 2, 36}};
    for (auto floppy : floppies)
    {
        CAPTURE(floppy.sectors);
        FILE *file = image_file(std::vector<uint8_t>(floppy.sectors * RAW_DISK_SECTOR_SIZE));
        auto media = open_raw("disk.img", file);
        REQUIRE(media != nullptr);
        CHECK(media->cylinderCount() == floppy.cylinders);
        CHECK(media->sideCount() == floppy.sides);
        CHECK(media->trackGeometry(0, 0).sectorCount == floppy.sectorsPerTrack);
        CHECK(media->trackGeometry(0, 0).encoding == TrackEncoding::MFM);
        remove_image(file);
    }
}

TEST_CASE("a boot sector that disagrees with the file size is ignored")
{
    // Claims 360K in a 720K file
    std::vector<uint8_t> image(1440 * RAW_DISK_SECTOR_SIZE, 0);
    const uint8_t boot[] = {0xEB, 0x3C, 0x90, 'M', 'T', 'O', 'O', '4', '0', '4', '8',
                            0x00, 0x02, 2, 1, 0, 2, 112, 0, 0xD0, 0x02, 0xFD, 2, 0, 9, 0, 2, 0};
    std::copy(std::begin(boot), std::end(boot), image.begin());
    FILE *file = image_file(image);
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);
    CHECK(media->cylinderCount() == 80);
    CHECK(media->sideCount() == 2);
    remove_image(file);
}

TEST_CASE("any other raw disk is one side of 9-sector tracks, the last one partial")
{
    FILE *file = image_file(numbered_sectors(100));
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);
    CHECK(media->cylinderCount() == 12);
    CHECK(media->sideCount() == 1);
    CHECK(media->trackGeometry(11, 0).sectorCount == 9);
    CHECK(media->trackGeometry(0, 0).encoding == TrackEncoding::UNKNOWN);
    CHECK(media->blockCount() == 100);
    remove_image(file);

    file = image_file({});
    media = open_raw("empty.img", file);
    REQUIRE(media != nullptr);
    CHECK(media->cylinderCount() == 0);
    CHECK(media->sideCount() == 0);
    remove_image(file);
}

TEST_CASE("every raw disk sector reads back as stored, and none past the end")
{
    auto image = numbered_sectors(4);
    FILE *file = image_file(image);
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);

    for (uint32_t sector : {2u, 0u, 1u, 3u})
    {
        ByteBuffer buffer;
        REQUIRE(media->readBlock(sector, buffer).is_success());
        auto start = image.begin() + sector * RAW_DISK_SECTOR_SIZE;
        CHECK(buffer == ByteBuffer(start, start + RAW_DISK_SECTOR_SIZE));
    }

    ByteBuffer buffer;
    CHECK(media->readBlock(4, buffer).is_error());
    remove_image(file);
}

TEST_CASE("a raw disk write is flushed to the file straight away")
{
    auto image = numbered_sectors(4);
    FILE *file = image_file(image);
    auto media = open_raw("disk.img", file);
    REQUIRE(media != nullptr);

    ByteBuffer sector(RAW_DISK_SECTOR_SIZE, 0x5A);
    REQUIRE(media->writeBlock(2, sector).is_success());

    std::copy(sector.begin(), sector.end(), image.begin() + 2 * RAW_DISK_SECTOR_SIZE);
    CHECK(flushed_contents() == image);
    remove_image(file);
}

TEST_CASE("a raw disk refuses a write when read-only, past the end, or of the wrong size")
{
    auto image = numbered_sectors(4);
    FILE *file = image_file(image);

    auto readOnly = open_raw("disk.img", file, true);
    REQUIRE(readOnly != nullptr);
    CHECK(readOnly->isReadOnly());
    CHECK(readOnly->writeBlock(0, ByteBuffer(RAW_DISK_SECTOR_SIZE, 0x11)).is_error());

    auto writable = open_raw("disk.img", file);
    REQUIRE(writable != nullptr);
    CHECK_FALSE(writable->isReadOnly());
    CHECK(writable->writeBlock(4, ByteBuffer(RAW_DISK_SECTOR_SIZE, 0x22)).is_error());
    CHECK(writable->writeBlock(1, ByteBuffer(RAW_DISK_SECTOR_SIZE - 1, 0x33)).is_error());

    CHECK(flushed_contents() == image);
    remove_image(file);
}

TEST_CASE("create() writes a zeroed image of the size asked for, and refuses an empty one")
{
    FILE *file = std::fopen(RAW_DISK_TEST_FILE, "w+b");
    REQUIRE(file != nullptr);
    REQUIRE(RawDisk::create(file, 256, 5).is_success());
    std::fflush(file);
    CHECK(flushed_contents() == std::vector<uint8_t>(256 * 5, 0));

    CHECK(RawDisk::create(file, 512, 0).is_error());
    CHECK(RawDisk::create(file, 0, 10).is_error());
    remove_image(file);
}
