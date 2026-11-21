#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <optional>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"

namespace
{

#define DDP_TEST_FILE "adam_disk_test.ddp"
#define DSK_TEST_FILE "adam_disk_test.dsk"

constexpr size_t BLOCK_SIZE = 1024;
constexpr size_t DSK_SECTOR_SIZE = 512;
constexpr uint8_t STATUS_BASE = 0x40;

// Reaches the handlers the bus calls, which are the only way in to a block
// read or write.
class TestDisk : public adamDisk
{
public:
    using adamDisk::adamnet_control_send;
    using adamDisk::deviceStatus;
};

// Every sector holds different bytes, so a read from the wrong place shows.
std::vector<uint8_t> numbered_sectors(size_t count, size_t sectorSize)
{
    std::vector<uint8_t> image(count * sectorSize);
    for (size_t sector = 0; sector < count; sector++)
        for (size_t offset = 0; offset < sectorSize; offset++)
            image[sector * sectorSize + offset] = (uint8_t)(sector * 3 + offset + 1);
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

void send(TestDisk &disk, ByteBuffer data)
{
    FujiAdamPacket packet(FUJI_DEVICEID::DISK, APT::MN_SEND, std::move(data));
    disk.adamnet_control_send(packet);
}

// The computer names a block in five bytes, and the drive answers with it.
std::optional<std::vector<uint8_t>> read_block(TestDisk &disk, uint32_t block)
{
    ByteBuffer request(5, 0);
    u32le_t number;
    number = block;
    memcpy(request.data(), &number, sizeof(number));

    bus.reset();
    send(disk, request);
    if (bus.outcome != Outcome::Data)
        return std::nullopt;
    return bus.answer;
}

// A write names the block first, then sends its 1024 bytes.
Outcome write_block(TestDisk &disk, uint32_t block, const std::vector<uint8_t> &data)
{
    read_block(disk, block);
    bus.reset();
    bus.incoming = data;
    send(disk, ByteBuffer(BLOCK_SIZE, 0));
    return bus.outcome;
}

// The DSK image keeps each block in two 512-byte sectors, laid out 5:1, so
// every four blocks share eight sectors.
std::vector<uint8_t> dsk_block(const std::vector<uint8_t> &image, uint32_t block)
{
    const size_t halves[4][2] = {{0, 5}, {2, 7}, {4, 1}, {6, 3}};
    size_t first_sector = block / 4 * 8;
    std::vector<uint8_t> data;
    for (size_t half : halves[block % 4])
    {
        auto sector = slice(image, (first_sector + half) * DSK_SECTOR_SIZE, DSK_SECTOR_SIZE);
        data.insert(data.end(), sector.begin(), sector.end());
    }
    return data;
}

void check_closed_once(TestDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

} // namespace

TEST_CASE("a .ddp mounts as DDP and the drive reports media")
{
    MemFile file(numbered_sectors(4, BLOCK_SIZE));
    TestDisk disk;
    CHECK(disk.deviceStatus().status == (STATUS_BASE | STATUS_NO_MEDIA));

    CHECK(disk.mount(&file, DDP_TEST_FILE, file.contents().size(), DISK_ACCESS_MODE_WRITE)
          .is_success());
    CHECK(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_DDP);
    CHECK(disk.deviceStatus().status == STATUS_BASE);
    check_closed_once(disk, file);
}

TEST_CASE("every DDP block reads back as stored, in any order")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (uint32_t block : {2, 0, 1, 3, 3, 1})
    {
        CAPTURE(block);
        CHECK(read_block(disk, block) == slice(image, block * BLOCK_SIZE, BLOCK_SIZE));
    }

    CHECK_FALSE(read_block(disk, 4).has_value());
    CHECK(bus.outcome == Outcome::Error);
    check_closed_once(disk, file);
}

TEST_CASE("a block number's upper half is ignored on a disk under 64K blocks")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    CHECK(read_block(disk, 0x10002) == slice(image, 2 * BLOCK_SIZE, BLOCK_SIZE));
    check_closed_once(disk, file);
}

TEST_CASE("a DDP write lands in its block and reads back")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    std::vector<uint8_t> data = filled(BLOCK_SIZE, 0x77);
    data[700] = 0x22;
    CHECK(write_block(disk, 2, data) == Outcome::Success);

    std::vector<uint8_t> expected = image;
    std::copy(data.begin(), data.end(), expected.begin() + 2 * BLOCK_SIZE);
    CHECK(file.contents() == expected);
    CHECK(read_block(disk, 2) == data);
    check_closed_once(disk, file);
}

TEST_CASE("a DDP write past the end is refused")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    CHECK(write_block(disk, 4, filled(BLOCK_SIZE, 0x77)) == Outcome::Error);
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("a disk mounted read-only refuses writes")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    CHECK(write_block(disk, 1, filled(BLOCK_SIZE, 0x77)) == Outcome::Error);
    CHECK(file.contents() == image);
    CHECK(read_block(disk, 1) == slice(image, BLOCK_SIZE, BLOCK_SIZE));
    check_closed_once(disk, file);
}

TEST_CASE("the config disk refuses writes")
{
    auto image = numbered_sectors(4, BLOCK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());
    disk.is_config_device = true;

    CHECK(write_block(disk, 1, filled(BLOCK_SIZE, 0x77)) == Outcome::Error);
    CHECK(file.contents() == image);
    CHECK(read_block(disk, 1) == slice(image, BLOCK_SIZE, BLOCK_SIZE));
    check_closed_once(disk, file);
}

TEST_CASE("DSK blocks are read from their two interleaved sectors")
{
    auto image = numbered_sectors(16, DSK_SECTOR_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(disk.device_active);

    for (uint32_t block : {5, 0, 1, 2, 3, 7, 4, 6})
    {
        CAPTURE(block);
        CHECK(read_block(disk, block) == dsk_block(image, block));
    }

    CHECK_FALSE(read_block(disk, 8).has_value());
    CHECK(bus.outcome == Outcome::Error);
    check_closed_once(disk, file);
}

TEST_CASE("a DSK write lands in the block's two sectors and reads back")
{
    auto image = numbered_sectors(16, DSK_SECTOR_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    std::vector<uint8_t> data = filled(DSK_SECTOR_SIZE, 0x11);
    data.resize(BLOCK_SIZE, 0x22);
    CHECK(write_block(disk, 6, data) == Outcome::Success);

    // Block 6 is the third of the second group: sectors 12 and 9.
    std::vector<uint8_t> expected = image;
    std::fill_n(expected.begin() + 12 * DSK_SECTOR_SIZE, DSK_SECTOR_SIZE, 0x11);
    std::fill_n(expected.begin() + 9 * DSK_SECTOR_SIZE, DSK_SECTOR_SIZE, 0x22);
    CHECK(file.contents() == expected);
    CHECK(read_block(disk, 6) == data);
    check_closed_once(disk, file);
}

TEST_CASE("unmount closes the image once and leaves the drive inactive")
{
    MemFile file(numbered_sectors(4, BLOCK_SIZE));
    TestDisk disk;
    REQUIRE(disk.mount(&file, DDP_TEST_FILE, file.contents().size(), DISK_ACCESS_MODE_READ)
            .is_success());

    check_closed_once(disk, file);
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK_FALSE(read_block(disk, 1).has_value());
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("mounting over a disk closes the old image once")
{
    auto first_image = numbered_sectors(4, BLOCK_SIZE);
    auto second_image = numbered_sectors(8, BLOCK_SIZE);
    MemFile first(first_image);
    MemFile second(second_image);
    TestDisk disk;

    REQUIRE(disk.mount(&first, DDP_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    REQUIRE(disk.mount(&second, DDP_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);
    CHECK(read_block(disk, 6) == slice(second_image, 6 * BLOCK_SIZE, BLOCK_SIZE));
    check_closed_once(disk, second);
}

TEST_CASE("a DDP under one block doesn't mount, and the drive is left inactive")
{
    std::vector<uint8_t> image(BLOCK_SIZE - 1, 0);
    MemFile file(image);
    TestDisk disk;

    CHECK(disk.mount(&file, DDP_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ).is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("a DSK under one block doesn't mount, and the drive is left inactive")
{
    std::vector<uint8_t> image(BLOCK_SIZE - 1, 0);
    MemFile file(image);
    TestDisk disk;

    CHECK(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ).is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("an image with an unknown extension doesn't mount, and its image is closed")
{
    std::vector<uint8_t> image(BLOCK_SIZE, 0);
    MemFile file(image);
    TestDisk disk;

    CHECK(disk.mount(&file, "adam_disk_test.xyz", image.size(), DISK_ACCESS_MODE_READ).is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
    disk.unmount();
    CHECK(file.closes == 1);
}
