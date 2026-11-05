#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"

namespace
{

#define ROM_TEST_FILE "drivewire_rom_mount_test.ccc"
#define CART_8K (8 * 1024)

std::string written_bytes()
{
    std::string bytes;
    for (const auto &packet : bus.sent)
        if (packet.command == CMD::NET_WRITE)
            bytes += packet.payload;
    return bytes;
}

// Whatever the outcome, the handle mount() was given is closed exactly once by
// the time the drive is unmounted.
void check_closed_once(drivewireDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

// A failed mount releases the drive straight away, not at the next unmount().
void check_nothing_mounted(drivewireDisk &disk, const MemFile &file)
{
    CHECK_FALSE(disk.device_active);
    CHECK(disk.disktype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
}

} // namespace

TEST_CASE("a ROM image goes to the DBC in 256-byte blocks between OPEN and CLOSE")
{
    bus.reset();
    auto image = make_image(CART_8K);
    MemFile file(image);

    drivewireDisk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_ROM);
    check_closed_once(disk, file);

    REQUIRE(bus.sent.size() == 2 + CART_8K / MEDIA_BLOCK_SIZE);
    for (const auto &packet : bus.sent)
        CHECK(packet.device == FUJI_DEVICEID::DBC);
    CHECK(bus.sent.front().command == CMD::NET_OPEN);
    CHECK(bus.sent.front().params == std::vector<uint32_t>{0});
    for (size_t idx = 1; idx + 1 < bus.sent.size(); idx++)
    {
        CHECK(bus.sent[idx].command == CMD::NET_WRITE);
        CHECK(bus.sent[idx].payload.size() == MEDIA_BLOCK_SIZE);
    }
    CHECK(bus.sent.back().command == CMD::NET_CLOSE);
    CHECK(written_bytes() == std::string(image.begin(), image.end()));
}

TEST_CASE("a .ccc or .rom mounts in either case")
{
    auto image = make_image(CART_8K);
    for (const char *filename : {"cart.ccc", "CART.CCC", "cart.rom", "Cart.Rom"})
    {
        INFO("filename := " << std::string(filename));
        bus.reset();
        MemFile file(image);

        drivewireDisk disk;
        CHECK(disk.mount(&file, filename, image.size(), DISK_ACCESS_MODE_READ) == MEDIATYPE_ROM);
        CHECK(written_bytes() == std::string(image.begin(), image.end()));
        check_closed_once(disk, file);
    }
}

TEST_CASE("an empty ROM image fails without touching the bus")
{
    bus.reset();
    MemFile file({});

    drivewireDisk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, 0, DISK_ACCESS_MODE_READ) == MEDIATYPE_UNKNOWN);
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
    CHECK(bus.sent.empty());
}

TEST_CASE("no reply to OPEN fails the mount and sends nothing more")
{
    bus.reset();
    bus.replies = {Reply::None};
    MemFile file(make_image(CART_8K));

    drivewireDisk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, CART_8K, DISK_ACCESS_MODE_READ)
          == MEDIATYPE_UNKNOWN);
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
    REQUIRE(bus.sent.size() == 1);
    CHECK(bus.sent[0].command == CMD::NET_OPEN);
}

TEST_CASE("no reply to a block fails the mount but still sends CLOSE")
{
    bus.reset();
    bus.replies = {Reply::Ack, Reply::Ack, Reply::None};
    MemFile file(make_image(CART_8K));

    drivewireDisk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, CART_8K, DISK_ACCESS_MODE_READ)
          == MEDIATYPE_UNKNOWN);
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
    REQUIRE(bus.sent.size() == 4);
    CHECK(bus.sent[3].command == CMD::NET_CLOSE);
}

TEST_CASE("a second ROM mount sends the new image and closes each handle once")
{
    auto first = make_image(CART_8K);
    auto second = make_image(CART_8K * 2);
    MemFile first_file(first);
    MemFile second_file(second);

    drivewireDisk disk;
    bus.reset();
    REQUIRE(disk.mount(&first_file, ROM_TEST_FILE, first.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_ROM);

    bus.reset();
    CHECK(disk.mount(&second_file, ROM_TEST_FILE, second.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_ROM);
    CHECK(written_bytes() == std::string(second.begin(), second.end()));
    check_closed_once(disk, second_file);
    CHECK(first_file.closes == 1);
}

TEST_CASE("every cartridge size mounts")
{
    for (size_t size : {CART_8K, CART_8K * 2, CART_8K * 4, CART_8K * 8, CART_8K * 16})
    {
        CAPTURE(size);
        bus.reset();
        auto image = make_image(size);
        MemFile file(image);

        drivewireDisk disk;
        CHECK(disk.mount(&file, ROM_TEST_FILE, size, DISK_ACCESS_MODE_READ) == MEDIATYPE_ROM);
        CHECK(written_bytes() == std::string(image.begin(), image.end()));
        check_closed_once(disk, file);
    }
}
