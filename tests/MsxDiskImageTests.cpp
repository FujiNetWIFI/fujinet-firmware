#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "MsxDiskImage.h"
#include "RawDisk.h"

namespace
{

#define MSX_DISK_TEST_FILE "msxdisk_test.tmp"

constexpr uint16_t DATE = (30 << 9) | (6 << 5) | 15; // 2010-06-15
constexpr uint16_t TIME = (13 << 11) | (45 << 5) | 21; // 13:45:42
constexpr uint32_t SERIAL = 0x12345678;

uint16_t le16(const std::vector<uint8_t> &image, size_t offset)
{
    return image[offset] | (image[offset + 1] << 8);
}

uint32_t le32(const std::vector<uint8_t> &image, size_t offset)
{
    return le16(image, offset) | (static_cast<uint32_t>(le16(image, offset + 2)) << 16);
}

// The template CONFIG ships has its own BPB; make it differ from what create writes.
struct BootTemplate
{
    uint8_t sector[512];
    BootTemplate()
    {
        memset(sector, 0xAA, sizeof(sector));
        sector[0] = 0xEB;
        sector[1] = 0x3C;
        sector[2] = 0x90;
        memcpy(sector + 3, "NEXTOR3 ", 8);
        memcpy(sector + 0x20, "VOL_ID", 6);
        sector[0x26] = 0;
    }
};

std::vector<uint8_t> pattern(size_t length, uint8_t seed)
{
    std::vector<uint8_t> data(length);
    for (size_t idx = 0; idx < length; idx++)
        data[idx] = static_cast<uint8_t>(seed + idx * 3 + 1);
    return data;
}

// Makes an image into the test file and returns its bytes; the file is left for the caller.
success_is_true make_image(uint32_t blocks, const std::vector<MsxDiskFile> &files,
                           const BootTemplate &boot = BootTemplate())
{
    FILE *file = std::fopen(MSX_DISK_TEST_FILE, "wb");
    REQUIRE(file != nullptr);
    auto result = MsxDiskImage::create(file, blocks, boot.sector, files, SERIAL, DATE, TIME);
    std::fclose(file);
    return result;
}

std::vector<uint8_t> read_image()
{
    FILE *file = std::fopen(MSX_DISK_TEST_FILE, "rb");
    REQUIRE(file != nullptr);
    std::fseek(file, 0, SEEK_END);
    std::vector<uint8_t> contents(std::ftell(file));
    std::fseek(file, 0, SEEK_SET);
    REQUIRE(std::fread(contents.data(), 1, contents.size(), file) == contents.size());
    std::fclose(file);
    return contents;
}

uint16_t fat_entry(const std::vector<uint8_t> &image, size_t fatOffset, uint16_t cluster)
{
    size_t offset = fatOffset + cluster * 3 / 2;
    uint16_t pair = le16(image, offset);
    return cluster % 2 == 0 ? pair & 0xFFF : pair >> 4;
}

std::vector<MsxDiskFile> two_files()
{
    return {{"MSXDOS  SYS", pattern(2432, 1)}, {"COMMAND COM", pattern(6400, 9)}};
}

struct Layout
{
    uint32_t blocks;
    uint8_t media;
    uint16_t heads;
    uint16_t sectorsPerFAT;
    size_t dataStart;
};

constexpr Layout LAYOUTS[] = {{720, 0xF8, 1, 2, 12}, {1440, 0xF9, 2, 3, 14}};

} // namespace

TEST_CASE("the image is exactly the requested size")
{
    for (const Layout &layout : LAYOUTS)
    {
        REQUIRE(make_image(layout.blocks, two_files()).is_success());
        CHECK(read_image().size() == layout.blocks * 512u);
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("the boot sector carries the geometry and the serial")
{
    BootTemplate boot;
    for (const Layout &layout : LAYOUTS)
    {
        REQUIRE(make_image(layout.blocks, two_files(), boot).is_success());
        auto image = read_image();

        CHECK(memcmp(image.data(), boot.sector, 0x0B) == 0);
        CHECK(le16(image, 0x0B) == 512);
        CHECK(image[0x0D] == 2);
        CHECK(le16(image, 0x0E) == 1);
        CHECK(image[0x10] == 2);
        CHECK(le16(image, 0x11) == 112);
        CHECK(le16(image, 0x13) == layout.blocks);
        CHECK(image[0x15] == layout.media);
        CHECK(le16(image, 0x16) == layout.sectorsPerFAT);
        CHECK(le16(image, 0x18) == 9);
        CHECK(le16(image, 0x1A) == layout.heads);
        CHECK(le16(image, 0x1C) == 0);
        CHECK(le32(image, 0x27) == SERIAL);
        CHECK(memcmp(image.data() + 0x1E, boot.sector + 0x1E, 0x27 - 0x1E) == 0);
        CHECK(memcmp(image.data() + 0x2B, boot.sector + 0x2B, 512 - 0x2B) == 0);
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("both FATs hold the media byte and the chains of each file")
{
    for (const Layout &layout : LAYOUTS)
    {
        REQUIRE(make_image(layout.blocks, two_files()).is_success());
        auto image = read_image();

        size_t fat1 = 512;
        size_t fat2 = fat1 + layout.sectorsPerFAT * 512;
        CHECK(memcmp(image.data() + fat1, image.data() + fat2, layout.sectorsPerFAT * 512) == 0);
        CHECK(image[fat1] == layout.media);
        CHECK(image[fat1 + 1] == 0xFF);
        CHECK(image[fat1 + 2] == 0xFF);

        // 2432 bytes take 3 clusters (2-4); 6400 take 7 (5-11).
        CHECK(fat_entry(image, fat1, 2) == 3);
        CHECK(fat_entry(image, fat1, 3) == 4);
        CHECK(fat_entry(image, fat1, 4) == 0xFFF);
        for (uint16_t cluster = 5; cluster < 11; cluster++)
            CHECK(fat_entry(image, fat1, cluster) == cluster + 1);
        CHECK(fat_entry(image, fat1, 11) == 0xFFF);
        CHECK(fat_entry(image, fat1, 12) == 0);
        CHECK(fat_entry(image, fat1, 13) == 0);
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("the root directory lists each file")
{
    for (const Layout &layout : LAYOUTS)
    {
        REQUIRE(make_image(layout.blocks, two_files()).is_success());
        auto image = read_image();

        size_t root = (1 + 2 * layout.sectorsPerFAT) * 512;
        CHECK(memcmp(image.data() + root, "MSXDOS  SYS", 11) == 0);
        CHECK(image[root + 11] == 0x20);
        CHECK(le16(image, root + 0x16) == TIME);
        CHECK(le16(image, root + 0x18) == DATE);
        CHECK(le16(image, root + 0x1A) == 2);
        CHECK(le32(image, root + 0x1C) == 2432);

        CHECK(memcmp(image.data() + root + 32, "COMMAND COM", 11) == 0);
        CHECK(image[root + 32 + 11] == 0x20);
        CHECK(le16(image, root + 32 + 0x16) == TIME);
        CHECK(le16(image, root + 32 + 0x18) == DATE);
        CHECK(le16(image, root + 32 + 0x1A) == 5);
        CHECK(le32(image, root + 32 + 0x1C) == 6400);

        for (size_t idx = root + 64; idx < root + 7 * 512; idx++)
            REQUIRE(image[idx] == 0);
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("file bytes sit at their clusters, padded with zeros")
{
    for (const Layout &layout : LAYOUTS)
    {
        auto files = two_files();
        REQUIRE(make_image(layout.blocks, files).is_success());
        auto image = read_image();

        size_t first = layout.dataStart * 512;
        size_t second = first + 3 * 1024;
        CHECK(memcmp(image.data() + first, files[0].data.data(), 2432) == 0);
        for (size_t idx = first + 2432; idx < second; idx++)
            REQUIRE(image[idx] == 0);
        CHECK(memcmp(image.data() + second, files[1].data.data(), 6400) == 0);
        size_t end = second + 7 * 1024;
        for (size_t idx = second + 6400; idx < image.size(); idx++)
            REQUIRE(image[idx] == 0);
        CHECK(end <= image.size());
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("an image with no files is formatted and empty")
{
    REQUIRE(make_image(720, {}).is_success());
    auto image = read_image();
    CHECK(image[512] == 0xF8);
    CHECK(fat_entry(image, 512, 2) == 0);
    for (size_t idx = 9 * 512; idx < image.size(); idx++)
        REQUIRE(image[idx] == 0);
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("a zero-length file takes no clusters")
{
    std::vector<MsxDiskFile> files = {{"EMPTY   TXT", {}}, {"MSXDOS  SYS", pattern(100, 5)}};
    REQUIRE(make_image(720, files).is_success());
    auto image = read_image();

    size_t root = 5 * 512;
    CHECK(le16(image, root + 0x1A) == 0);
    CHECK(le32(image, root + 0x1C) == 0);
    CHECK(le16(image, root + 32 + 0x1A) == 2);
    CHECK(fat_entry(image, 512, 2) == 0xFFF);
    CHECK(fat_entry(image, 512, 3) == 0);
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("a bad block count writes nothing")
{
    for (uint32_t blocks : {0u, 719u, 1000u, 2880u})
    {
        REQUIRE(make_image(blocks, {}).is_error());
        CHECK(read_image().empty());
    }
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("a boot sector that isn't a Nextor one is refused")
{
    BootTemplate boot;
    boot.sector[0] = 0x00;
    CHECK(make_image(720, {}, boot).is_error());

    BootTemplate noVolumeId;
    noVolumeId.sector[0x22] = 'X';
    CHECK(make_image(720, {}, noVolumeId).is_error());

    BootTemplate nearJump;
    nearJump.sector[0] = 0xE9;
    CHECK(make_image(720, {}, nearJump).is_success());
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("files that don't fit are refused")
{
    // 360K has 354 data clusters of 1024 bytes.
    CHECK(make_image(720, {{"BIG     BIN", pattern(354 * 1024 + 1, 1)}}).is_error());
    CHECK(make_image(720, {{"FULL    BIN", pattern(354 * 1024, 1)}}).is_success());
    CHECK(make_image(720, {{"ONE     BIN", pattern(200 * 1024, 1)},
                           {"TWO     BIN", pattern(155 * 1024, 2)}})
              .is_error());
    std::remove(MSX_DISK_TEST_FILE);
}

TEST_CASE("a raw disk finds the floppy geometry in the new boot sector")
{
    struct Expected
    {
        uint32_t blocks;
        uint8_t sides;
    };
    for (Expected expected : {Expected{720, 1}, Expected{1440, 2}})
    {
        REQUIRE(make_image(expected.blocks, two_files()).is_success());
        FILE *file = std::fopen(MSX_DISK_TEST_FILE, "r+b");
        REQUIRE(file != nullptr);
        auto disk = Media::open<RawDisk>(MSX_DISK_TEST_FILE, file, nullptr, false);
        REQUIRE(disk != nullptr);
        CHECK(disk->blockCount() == expected.blocks);
        CHECK(disk->cylinderCount() == 80);
        CHECK(disk->sideCount() == expected.sides);
        CHECK(disk->trackGeometry(0, 0).sectorCount == 9);
        disk->close();
    }
    std::remove(MSX_DISK_TEST_FILE);
}
