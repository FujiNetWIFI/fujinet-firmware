#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"
#include "IMDFixture.h"

namespace
{

#define IMG_TEST_FILE "rs232_disk_test.img"
#define IMD_TEST_FILE "rs232_disk_test.imd"
#define ALTAIR_TEST_FILE "rs232_disk_test.dsk"

constexpr size_t IMG_SECTOR_SIZE = 512;
constexpr size_t IMD_SECTOR_SIZE = 128;
constexpr size_t ALTAIR_MINIDISK_SIZE = 76720;
constexpr uint32_t ALTAIR_MINIDISK_SECTORS = 35 * 16;

// Reaches the command handler the bus calls, which is the only way in to a
// sector read or write.
class TestDisk : public rs232Disk
{
public:
    using rs232Disk::rs232_process;
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

std::vector<uint8_t> sector_of(const std::vector<uint8_t> &image, size_t sector,
                               size_t sectorSize)
{
    return std::vector<uint8_t>(image.begin() + sector * sectorSize,
                                image.begin() + (sector + 1) * sectorSize);
}

std::optional<std::vector<uint8_t>> read_sector(TestDisk &disk, uint32_t sector)
{
    bus.reset();
    disk.rs232_process(FujiBusPacket(FUJI_DEVICEID::DISK, CMD::DISK_READ, sector));
    if (bus.outcome != Outcome::Data)
        return std::nullopt;
    return bus.answer;
}

// PUT writes; WRITE writes with verify.
Outcome write_sector(TestDisk &disk, uint32_t sector, const std::vector<uint8_t> &data,
                     fujiCommandID_t command = CMD::DISK_PUT)
{
    bus.reset();
    bus.incoming = data;
    disk.rs232_process(FujiBusPacket(FUJI_DEVICEID::DISK, command, sector));
    return bus.outcome;
}

std::vector<uint8_t> drive_status(TestDisk &disk)
{
    bus.reset();
    disk.rs232_process(FujiBusPacket(FUJI_DEVICEID::DISK, CMD::DISK_STATUS));
    return bus.answer;
}

void check_closed_once(TestDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

// 2 cylinders x 2 heads x 4 sectors of 128 bytes, each track filled with its
// own byte: $A0, $A1, $B0, and $B1 in LBA order.
std::vector<uint8_t> imd_two_by_two()
{
    IMD builder;
    builder.header();
    builder.simple_track(3, 0, 0, 4, 0xA0);
    builder.simple_track(3, 0, 1, 4, 0xA1);
    builder.simple_track(3, 1, 0, 4, 0xB0);
    builder.simple_track(3, 1, 1, 4, 0xB1);
    return builder.out;
}

std::vector<uint8_t> filled(size_t length, uint8_t value)
{
    return std::vector<uint8_t>(length, value);
}

} // namespace

// systemBus's first virtual, so its vtable is emitted here.
void systemBus::addDevice(virtualDevice *, fujiDeviceID_t) {}

TEST_CASE("an .img mounts raw, writable or read-only")
{
    for (auto mode : {DISK_ACCESS_MODE_READ, DISK_ACCESS_MODE_WRITE})
    {
        MemFile file(numbered_sectors(4, IMG_SECTOR_SIZE));
        TestDisk disk;
        CHECK(disk.mount(&file, IMG_TEST_FILE, file.contents().size(), mode).is_success());
        CHECK(disk.device_active);
        CHECK(disk.mediatype() == MEDIATYPE_IMG);
        CHECK(disk.mount_time() != 0);
        check_closed_once(disk, file);
    }
}

TEST_CASE("every IMG sector reads back as stored, in any order")
{
    auto image = numbered_sectors(4, IMG_SECTOR_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, IMG_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (uint32_t sector : {2, 0, 1, 3, 3, 1})
    {
        CAPTURE(sector);
        CHECK(read_sector(disk, sector) == sector_of(image, sector, IMG_SECTOR_SIZE));
    }

    CHECK_FALSE(read_sector(disk, 4).has_value());
    CHECK(bus.outcome == Outcome::DataWithError);
    check_closed_once(disk, file);
}

TEST_CASE("an IMG write lands in its sector and reads back, with or without verify")
{
    for (auto command : {CMD::DISK_PUT, CMD::DISK_WRITE})
    {
        auto image = numbered_sectors(4, IMG_SECTOR_SIZE);
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, IMG_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
                .is_success());

        REQUIRE(read_sector(disk, 2).has_value());
        CHECK(write_sector(disk, 2, filled(IMG_SECTOR_SIZE, 0x77), command)
              == Outcome::Success);

        std::vector<uint8_t> expected = image;
        std::fill_n(expected.begin() + 2 * IMG_SECTOR_SIZE, IMG_SECTOR_SIZE, 0x77);
        CHECK(file.contents() == expected);
        CHECK(read_sector(disk, 2) == filled(IMG_SECTOR_SIZE, 0x77));
        check_closed_once(disk, file);
    }
}

TEST_CASE("an IMG write past the end, or short of a sector, is refused")
{
    auto image = numbered_sectors(4, IMG_SECTOR_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, IMG_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    CHECK(write_sector(disk, 4, filled(IMG_SECTOR_SIZE, 0x77)) == Outcome::Error);
    CHECK(write_sector(disk, 1, filled(IMG_SECTOR_SIZE - 1, 0x77)) == Outcome::Error);
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("unmount closes the image once and leaves nothing to read or write")
{
    MemFile file(numbered_sectors(4, IMG_SECTOR_SIZE));
    TestDisk disk;
    REQUIRE(disk.mount(&file, IMG_TEST_FILE, file.contents().size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    check_closed_once(disk, file);
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mount_time() == 0);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK_FALSE(read_sector(disk, 0).has_value());
    CHECK(bus.outcome == Outcome::Error);
    CHECK(write_sector(disk, 0, filled(IMG_SECTOR_SIZE, 0x77)) == Outcome::Error);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("mounting over a disk closes the old image once")
{
    auto first_image = numbered_sectors(4, IMG_SECTOR_SIZE);
    auto second_image = numbered_sectors(8, IMG_SECTOR_SIZE);
    MemFile first(first_image);
    MemFile second(second_image);
    TestDisk disk;

    REQUIRE(disk.mount(&first, IMG_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    REQUIRE(disk.mount(&second, IMG_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);
    CHECK(read_sector(disk, 6) == sector_of(second_image, 6, IMG_SECTOR_SIZE));
    check_closed_once(disk, second);
}

TEST_CASE("IMD sectors read back in LBA order")
{
    auto image = imd_two_by_two();
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, IMD_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(disk.device_active);

    const uint8_t track_fill[] = {0xA0, 0xA1, 0xB0, 0xB1};
    for (uint32_t lba : {9, 0, 5, 15, 2})
    {
        CAPTURE(lba);
        CHECK(read_sector(disk, lba) == filled(IMD_SECTOR_SIZE, track_fill[lba / 4]));
    }
    CHECK_FALSE(read_sector(disk, 16).has_value());
    check_closed_once(disk, file);
}

TEST_CASE("an IMD image is found by its contents under another name")
{
    auto image = imd_two_by_two();
    MemFile file(image);
    TestDisk disk;
    CHECK(disk.mount(&file, "rs232_disk_test.dat", image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(read_sector(disk, 4) == filled(IMD_SECTOR_SIZE, 0xA1));
    check_closed_once(disk, file);
}

TEST_CASE("a writable IMD stores a sector in place, and a read-only one refuses")
{
    SUBCASE("writable")
    {
        auto image = imd_two_by_two();
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, IMD_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
                .is_success());

        std::vector<uint8_t> data = filled(IMD_SECTOR_SIZE, 0x11);
        data[64] = 0x22;
        CHECK(write_sector(disk, 6, data) == Outcome::Success);
        CHECK(read_sector(disk, 6) == data);
        CHECK(read_sector(disk, 5) == filled(IMD_SECTOR_SIZE, 0xA1));
        CHECK(write_sector(disk, 16, data) == Outcome::Error);
        check_closed_once(disk, file);
    }

    SUBCASE("read-only")
    {
        auto image = imd_two_by_two();
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, IMD_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
                .is_success());

        CHECK(write_sector(disk, 6, filled(IMD_SECTOR_SIZE, 0x5A)) == Outcome::Error);
        CHECK(file.contents() == image);
        auto status = drive_status(disk);
        REQUIRE(status.size() == 4);
        CHECK((status[0] & DISK_DRIVE_STATUS_WRITE_PROTECT_ERROR) != 0);
        check_closed_once(disk, file);
    }
}

TEST_CASE("an Altair-sized .dsk mounts as Altair, with 137-byte sectors")
{
    auto image = numbered_sectors(ALTAIR_MINIDISK_SECTORS, ALTAIR_SECTOR_SIZE);
    REQUIRE(image.size() == ALTAIR_MINIDISK_SIZE);
    MemFile file(image);
    TestDisk disk;
    REQUIRE(disk.mount(&file, ALTAIR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (uint32_t sector : {300u, 0u, 1u, ALTAIR_MINIDISK_SECTORS - 1})
    {
        CAPTURE(sector);
        CHECK(read_sector(disk, sector) == sector_of(image, sector, ALTAIR_SECTOR_SIZE));
    }
    CHECK_FALSE(read_sector(disk, ALTAIR_MINIDISK_SECTORS).has_value());
    check_closed_once(disk, file);
}

TEST_CASE("an Altair write lands in its sector, and a read-only mount refuses")
{
    for (auto mode : {DISK_ACCESS_MODE_READ, DISK_ACCESS_MODE_WRITE})
    {
        auto image = numbered_sectors(ALTAIR_MINIDISK_SECTORS, ALTAIR_SECTOR_SIZE);
        MemFile file(image);
        TestDisk disk;
        REQUIRE(disk.mount(&file, ALTAIR_TEST_FILE, image.size(), mode).is_success());

        std::vector<uint8_t> expected = image;
        if (mode == DISK_ACCESS_MODE_WRITE)
        {
            CHECK(write_sector(disk, 7, filled(ALTAIR_SECTOR_SIZE, 0x77)) == Outcome::Success);
            std::fill_n(expected.begin() + 7 * ALTAIR_SECTOR_SIZE, ALTAIR_SECTOR_SIZE, 0x77);
            CHECK(read_sector(disk, 7) == filled(ALTAIR_SECTOR_SIZE, 0x77));
        }
        else
        {
            CHECK(write_sector(disk, 7, filled(ALTAIR_SECTOR_SIZE, 0x77)) == Outcome::Error);
        }
        CHECK(write_sector(disk, ALTAIR_MINIDISK_SECTORS, filled(ALTAIR_SECTOR_SIZE, 0x77))
              == Outcome::Error);
        CHECK(file.contents() == expected);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a .dsk that isn't an Altair size mounts raw")
{
    auto image = numbered_sectors(4, IMG_SECTOR_SIZE);
    MemFile file(image);
    TestDisk disk;
    CHECK(disk.mount(&file, ALTAIR_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(read_sector(disk, 3) == sector_of(image, 3, IMG_SECTOR_SIZE));
    check_closed_once(disk, file);
}

TEST_CASE("an IMD with no tracks doesn't mount, and the drive is left inactive")
{
    IMD builder;
    builder.header();
    MemFile file(builder.out);
    TestDisk disk;

    CHECK(disk.mount(&file, IMD_TEST_FILE, builder.out.size(), DISK_ACCESS_MODE_READ)
            .is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mount_time() == 0);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

TEST_CASE("an Altair image of the wrong size doesn't mount, and the drive is left inactive")
{
    std::vector<uint8_t> image(1000, 0);
    MemFile file(image);
    TestDisk disk;

    CHECK(disk.mount(&file, "rs232_disk_test.dsk", image.size(), DISK_ACCESS_MODE_READ,
                     MEDIATYPE_ALTAIR)
              .is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mount_time() == 0);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}
