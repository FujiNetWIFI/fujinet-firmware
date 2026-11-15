#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <optional>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"

extern spError_t last_sp_error;

namespace
{

#define PO_TEST_FILE "iwm_disk_test.po"
#define DO_TEST_FILE "iwm_disk_test.do"
#define DSK_TEST_FILE "iwm_disk_test.dsk"
#define TWO_MG_TEST_FILE "iwm_disk_test.2mg"

constexpr size_t BLOCK_SIZE = 512;
constexpr size_t SECTOR_SIZE = 256;
constexpr size_t TRACK_SIZE = 4096;
constexpr uint32_t FLOPPY_BLOCKS = 280; // 35 tracks of 4K
constexpr size_t TWO_MG_HEADER_SIZE = 64;

// Reaches the handlers the bus calls, which are the only way in to a block
// read or write.
class TestDisk : public iwmDisk
{
public:
    using iwmDisk::iwm_readblock;
    using iwmDisk::iwm_writeblock;
};

// Every 256-byte half of every block holds different bytes, so a read from the
// wrong place, or with its halves swapped, shows.
std::vector<uint8_t> numbered_blocks(uint32_t count, size_t header = 0)
{
    std::vector<uint8_t> image(header + count * BLOCK_SIZE, 0);
    for (uint32_t block = 0; block < count; block++)
        for (size_t offset = 0; offset < BLOCK_SIZE; offset++)
            image[header + block * BLOCK_SIZE + offset] =
                (uint8_t)(block * 3 + offset + offset / SECTOR_SIZE + 1);
    return image;
}

std::vector<uint8_t> slice(const std::vector<uint8_t> &image, size_t start, size_t length)
{
    return std::vector<uint8_t>(image.begin() + start, image.begin() + start + length);
}

std::vector<uint8_t> filled(size_t length, uint8_t value)
{
    return std::vector<uint8_t>(length, value);
}

// A DOS-order image keeps each ProDOS block in two 256-byte sectors of its
// track, in this order.
const size_t DO_SECTORS[8][2] = {{0, 14}, {13, 12}, {11, 10}, {9, 8},
                                 {7, 6}, {5, 4}, {3, 2}, {1, 15}};

std::vector<uint8_t> do_block(const std::vector<uint8_t> &image, uint32_t block)
{
    size_t track = block / 8 * TRACK_SIZE;
    std::vector<uint8_t> data;
    for (size_t sector : DO_SECTORS[block % 8])
    {
        auto half = slice(image, track + sector * SECTOR_SIZE, SECTOR_SIZE);
        data.insert(data.end(), half.begin(), half.end());
    }
    return data;
}

std::vector<uint8_t> two_mg_image(uint32_t blocks)
{
    auto image = numbered_blocks(blocks, TWO_MG_HEADER_SIZE);
    memcpy(image.data(), "2IMG", 4);
    return image;
}

void send(TestDisk &disk, bool write, uint32_t block)
{
    iwm_decoded_cmd_t cmd;
    memset(&cmd.frame, 0, sizeof(cmd.frame));
    cmd.frame.block_rw.num = block;
    if (write)
        disk.iwm_writeblock(cmd);
    else
        disk.iwm_readblock(cmd);
}

std::optional<std::vector<uint8_t>> read_block(TestDisk &disk, uint32_t block)
{
    bus.reset();
    last_sp_error = SP_ERR::NOERROR;
    send(disk, false, block);
    if (bus.outcome != Outcome::Data)
        return std::nullopt;
    return bus.answer;
}

Outcome write_block(TestDisk &disk, uint32_t block, const std::vector<uint8_t> &data)
{
    bus.reset();
    last_sp_error = SP_ERR::NOERROR;
    bus.incoming = data;
    send(disk, true, block);
    return bus.outcome;
}

void check_closed_once(TestDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

} // namespace

TEST_CASE("every PO block reads back as stored, in any order")
{
    auto image = numbered_blocks(8);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, PO_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_PO);
    CHECK(disk.device_active);
    CHECK(disk.disktype() == MEDIATYPE_PO);

    for (uint32_t block : {3, 0, 1, 7, 7, 2})
    {
        CAPTURE(block);
        CHECK(read_block(disk, block) == slice(image, block * BLOCK_SIZE, BLOCK_SIZE));
    }

    CHECK_FALSE(read_block(disk, 8).has_value());
    CHECK(bus.outcome == Outcome::Error);
    CHECK(last_sp_error == SP_ERR::IOERROR);
    check_closed_once(disk, file);
}

TEST_CASE("a PO write lands in its block and reads back")
{
    auto image = numbered_blocks(8);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, PO_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            == MEDIATYPE_PO);

    std::vector<uint8_t> data = filled(BLOCK_SIZE, 0x77);
    data[300] = 0x22;
    CHECK(write_block(disk, 5, data) == Outcome::Success);

    std::vector<uint8_t> expected = image;
    std::copy(data.begin(), data.end(), expected.begin() + 5 * BLOCK_SIZE);
    CHECK(file.contents() == expected);
    CHECK(read_block(disk, 5) == data);
    check_closed_once(disk, file);
}

TEST_CASE("a read-only disk refuses writes as write-protected")
{
    auto image = numbered_blocks(8);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, PO_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_PO);

    CHECK(write_block(disk, 5, filled(BLOCK_SIZE, 0x77)) == Outcome::Error);
    CHECK(last_sp_error == SP_ERR::NOWRITE);
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("DO blocks are read from their two interleaved sectors")
{
    auto image = numbered_blocks(FLOPPY_BLOCKS);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DO_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_DO);

    for (uint32_t block : {0, 1, 2, 3, 4, 5, 6, 7, 8, 279})
    {
        CAPTURE(block);
        CHECK(read_block(disk, block) == do_block(image, block));
    }
    check_closed_once(disk, file);
}

TEST_CASE("a DO write lands in the block's two sectors and reads back")
{
    auto image = numbered_blocks(FLOPPY_BLOCKS);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DO_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            == MEDIATYPE_DO);

    std::vector<uint8_t> data = filled(SECTOR_SIZE, 0x11);
    data.resize(BLOCK_SIZE, 0x22);
    CHECK(write_block(disk, 9, data) == Outcome::Success);

    // Block 9 is the second of track 1: sectors 13 and 12.
    std::vector<uint8_t> expected = image;
    std::fill_n(expected.begin() + TRACK_SIZE + 13 * SECTOR_SIZE, SECTOR_SIZE, 0x11);
    std::fill_n(expected.begin() + TRACK_SIZE + 12 * SECTOR_SIZE, SECTOR_SIZE, 0x22);
    CHECK(file.contents() == expected);
    CHECK(read_block(disk, 9) == data);
    check_closed_once(disk, file);
}

TEST_CASE("a .dsk is DOS order unless block 2 holds a ProDOS volume header")
{
    SUBCASE("DOS order")
    {
        auto image = numbered_blocks(FLOPPY_BLOCKS);
        MemFile file(image);
        TestDisk disk;
        CHECK(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              == MEDIATYPE_DO);
        CHECK(read_block(disk, 3) == do_block(image, 3));
        check_closed_once(disk, file);
    }

    SUBCASE("ProDOS order")
    {
        auto image = numbered_blocks(FLOPPY_BLOCKS);
        uint8_t *header = image.data() + 2 * BLOCK_SIZE;
        header[0] = 0; // no previous directory block
        header[1] = 0;
        header[4] = 0xF0; // volume directory header
        u16le_t totalBlocks;
        totalBlocks = (uint16_t)FLOPPY_BLOCKS;
        memcpy(header + 0x29, &totalBlocks, sizeof(totalBlocks));
        MemFile file(image);
        TestDisk disk;
        CHECK(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              == MEDIATYPE_PO);
        CHECK(read_block(disk, 3) == slice(image, 3 * BLOCK_SIZE, BLOCK_SIZE));
        check_closed_once(disk, file);
    }
}

TEST_CASE("2MG blocks start after the header, whatever the name says")
{
    for (const char *name : {TWO_MG_TEST_FILE, PO_TEST_FILE})
    {
        CAPTURE(name);
        auto image = two_mg_image(8);
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, name, image.size(), DISK_ACCESS_MODE_WRITE)
                == MEDIATYPE_2MG);

        CHECK(read_block(disk, 6)
              == slice(image, TWO_MG_HEADER_SIZE + 6 * BLOCK_SIZE, BLOCK_SIZE));
        CHECK(write_block(disk, 1, filled(BLOCK_SIZE, 0x77)) == Outcome::Success);
        CHECK(slice(file.contents(), TWO_MG_HEADER_SIZE + BLOCK_SIZE, BLOCK_SIZE)
              == filled(BLOCK_SIZE, 0x77));
        CHECK(slice(file.contents(), 0, TWO_MG_HEADER_SIZE)
              == slice(image, 0, TWO_MG_HEADER_SIZE));
        check_closed_once(disk, file);
    }
}

TEST_CASE("unmount closes the image once and the drive reports offline")
{
    auto image = numbered_blocks(8);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, PO_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            == MEDIATYPE_PO);

    check_closed_once(disk, file);
    CHECK_FALSE(disk.device_active);
    CHECK(disk.disktype() == MEDIATYPE_UNKNOWN);
    CHECK_FALSE(read_block(disk, 1).has_value());
    CHECK(last_sp_error == SP_ERR::OFFLINE);
    CHECK(write_block(disk, 1, filled(BLOCK_SIZE, 0x77)) == Outcome::Error);
    CHECK(last_sp_error == SP_ERR::OFFLINE);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("mounting over a disk closes the old image and reports the switch once")
{
    auto first_image = numbered_blocks(8);
    auto second_image = numbered_blocks(16);
    MemFile first(first_image);
    MemFile second(second_image);
    TestDisk disk;

    REQUIRE(disk.mount(&first, PO_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_PO);
    REQUIRE(disk.mount(&second, PO_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_PO);
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);

    SUBCASE("a block past 2 reports offline once, then reads")
    {
        CHECK_FALSE(read_block(disk, 12).has_value());
        CHECK(last_sp_error == SP_ERR::OFFLINE);
        CHECK(read_block(disk, 12) == slice(second_image, 12 * BLOCK_SIZE, BLOCK_SIZE));
    }

    SUBCASE("blocks 0 to 2 read straight away and clear the switch")
    {
        CHECK(read_block(disk, 2) == slice(second_image, 2 * BLOCK_SIZE, BLOCK_SIZE));
        CHECK(read_block(disk, 12) == slice(second_image, 12 * BLOCK_SIZE, BLOCK_SIZE));
    }

    check_closed_once(disk, second);
}
