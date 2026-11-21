#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <optional>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"

namespace
{

#define ATR_TEST_FILE "sio_disk_test.atr"

constexpr size_t ATR_HEADER_SIZE = 16;
constexpr size_t BOOT_SECTOR_SIZE = 128;
constexpr uint16_t BOOT_SECTORS = 3;

// Reaches the command handler the bus calls, which is the only way in to a
// sector read or write.
class TestDisk : public sioDisk
{
public:
    using sioDisk::sio_process;
};

// ATR keeps the three boot sectors at 128 bytes whatever the disk's sector size.
size_t sector_length(uint16_t sector, size_t sectorSize)
{
    return sector <= BOOT_SECTORS ? BOOT_SECTOR_SIZE : sectorSize;
}

size_t sector_offset(uint16_t sector, size_t sectorSize)
{
    if (sector <= BOOT_SECTORS)
        return ATR_HEADER_SIZE + (sector - 1) * BOOT_SECTOR_SIZE;
    return ATR_HEADER_SIZE + BOOT_SECTORS * BOOT_SECTOR_SIZE + (sector - 4) * sectorSize;
}

// An ATR with every sector holding different bytes, so a read from the wrong
// place shows.
std::vector<uint8_t> atr_image(uint16_t sectors, size_t sectorSize)
{
    size_t dataSize = sector_offset(sectors, sectorSize) + sectorSize - ATR_HEADER_SIZE;
    std::vector<uint8_t> image(ATR_HEADER_SIZE + dataSize, 0);

    struct __attribute__((packed))
    {
        u16le_t magic;
        u16le_t paragraphs;
        u16le_t sectorSize;
        uint8_t paragraphsHigh;
    } header;
    header.magic = 0x0296;
    header.paragraphs = (uint16_t)(dataSize / 16);
    header.sectorSize = (uint16_t)sectorSize;
    header.paragraphsHigh = 0; // the test images are far under 1M
    memcpy(image.data(), &header, sizeof(header));

    for (uint16_t sector = 1; sector <= sectors; sector++)
    {
        size_t start = sector_offset(sector, sectorSize);
        for (size_t offset = 0; offset < sector_length(sector, sectorSize); offset++)
            image[start + offset] = (uint8_t)(sector * 3 + offset + 1);
    }
    return image;
}

std::vector<uint8_t> sector_of(const std::vector<uint8_t> &image, uint16_t sector,
                               size_t sectorSize)
{
    auto start = image.begin() + sector_offset(sector, sectorSize);
    return std::vector<uint8_t>(start, start + sector_length(sector, sectorSize));
}

void send(TestDisk &disk, fujiCommandID_t command, uint16_t sector)
{
    FujiSIOPacket packet;
    packet.frame.device = FUJI_DEVICEID::DISK;
    packet.frame.comnd = command;
    packet.frame.aux12 = sector;
    disk.sio_process(packet);
}

std::optional<std::vector<uint8_t>> read_sector(TestDisk &disk, uint16_t sector)
{
    bus.reset();
    send(disk, CMD::DISK_READ, sector);
    if (bus.outcome != Outcome::Data)
        return std::nullopt;
    return bus.answer;
}

// PUT writes; WRITE writes with verify.
Outcome write_sector(TestDisk &disk, uint16_t sector, const std::vector<uint8_t> &data,
                     fujiCommandID_t command = CMD::DISK_PUT)
{
    bus.reset();
    bus.incoming = data;
    send(disk, command, sector);
    return bus.outcome;
}

std::vector<uint8_t> filled(size_t length, uint8_t value)
{
    return std::vector<uint8_t>(length, value);
}

void check_closed_once(TestDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

} // namespace

TEST_CASE("every single-density ATR sector reads back as stored, in any order")
{
    auto image = atr_image(10, 128);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_ATR);

    for (uint16_t sector : {5, 1, 2, 10, 10, 3})
    {
        CAPTURE(sector);
        CHECK(read_sector(disk, sector) == sector_of(image, sector, 128));
    }
    check_closed_once(disk, file);
}

TEST_CASE("a double-density ATR keeps its three boot sectors at 128 bytes")
{
    auto image = atr_image(10, 256);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (uint16_t sector : {4, 1, 3, 2, 10, 5})
    {
        CAPTURE(sector);
        auto data = read_sector(disk, sector);
        REQUIRE(data.has_value());
        CHECK(data->size() == sector_length(sector, 256));
        CHECK(*data == sector_of(image, sector, 256));
    }
    check_closed_once(disk, file);
}

TEST_CASE("sector 0 and a sector past the end are refused")
{
    auto image = atr_image(10, 128);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (uint16_t sector : {0, 11})
    {
        CAPTURE(sector);
        CHECK_FALSE(read_sector(disk, sector).has_value());
        CHECK(bus.outcome == Outcome::Error);
    }
    check_closed_once(disk, file);
}

TEST_CASE("an ATR write lands in its sector and reads back, with or without verify")
{
    for (auto command : {CMD::DISK_PUT, CMD::DISK_WRITE})
    {
        auto image = atr_image(10, 256);
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
                .is_success());

        REQUIRE(read_sector(disk, 6).has_value());
        CHECK(write_sector(disk, 6, filled(256, 0x77), command) == Outcome::Success);
        CHECK(write_sector(disk, 2, filled(128, 0x55), command) == Outcome::Success);

        std::vector<uint8_t> expected = image;
        std::fill_n(expected.begin() + sector_offset(6, 256), 256, 0x77);
        std::fill_n(expected.begin() + sector_offset(2, 256), 128, 0x55);
        CHECK(file.contents() == expected);
        CHECK(read_sector(disk, 6) == filled(256, 0x77));
        CHECK(read_sector(disk, 2) == filled(128, 0x55));
        check_closed_once(disk, file);
    }
}

TEST_CASE("an ATR write past the end, or short of a sector, is refused")
{
    auto image = atr_image(10, 128);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    CHECK(write_sector(disk, 11, filled(128, 0x77)) == Outcome::Error);
    CHECK(write_sector(disk, 4, filled(127, 0x77)) == Outcome::Error);
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("unmount closes the image once and the drive stops answering")
{
    auto image = atr_image(10, 128);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    check_closed_once(disk, file);
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK_FALSE(read_sector(disk, 1).has_value());
    CHECK(bus.outcome == Outcome::None);
    CHECK(write_sector(disk, 4, filled(128, 0x77)) == Outcome::None);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("mounting over a disk closes the old image once")
{
    auto first_image = atr_image(10, 128);
    auto second_image = atr_image(20, 256);
    MemFile first(first_image);
    MemFile second(second_image);
    TestDisk disk;

    REQUIRE(disk.mount(&first, ATR_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    REQUIRE(disk.mount(&second, ATR_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);
    CHECK(read_sector(disk, 15) == sector_of(second_image, 15, 256));
    check_closed_once(disk, second);
}

TEST_CASE("an image without the ATR header doesn't mount, and the drive doesn't answer")
{
    auto image = atr_image(10, 128);
    image[0] = 0;
    MemFile file(image);
    TestDisk disk;
    CHECK(disk.mount(&file, ATR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_error());
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK_FALSE(read_sector(disk, 1).has_value());
    CHECK(bus.outcome == Outcome::None);
    check_closed_once(disk, file);
}

TEST_CASE("an image without the ATX header doesn't mount, and the file is closed")
{
    std::vector<uint8_t> shortImage(8, 0);
    std::vector<uint8_t> badMagic(64, 0);
    for (auto *image : {&shortImage, &badMagic})
    {
        MemFile file(*image);
        TestDisk disk;
        CHECK(disk.mount(&file, "sio_disk_test.atx", image->size(), DISK_ACCESS_MODE_READ)
                .is_error());
        CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
        check_closed_once(disk, file);
    }
}

TEST_CASE("an XEX that can't load its bootloader doesn't mount, and the file is closed")
{
    std::vector<uint8_t> image(64, 0);
    MemFile file(image);
    TestDisk disk;
    CHECK(disk.mount(&file, "sio_disk_test.xex", image.size(), DISK_ACCESS_MODE_READ)
            .is_error());
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    check_closed_once(disk, file);
}
