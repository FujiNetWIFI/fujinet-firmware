#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Cartridge.h"
#include "CoCoROMFormats.h"
#include "disk.h"
#include "romMountStubs.h"

namespace
{

#define ROM_TEST_FILE "drivewire_rom_mount_test.ccc"
#define CART_4K 4_kB
#define CART_8K 8_kB

using Params = std::vector<uint32_t>;

std::vector<fujiCommandID_t> commands()
{
    std::vector<fujiCommandID_t> sent;
    for (const auto &packet : bus.sent)
        sent.push_back(packet.command);
    return sent;
}

std::vector<Params> params_of(fujiCommandID_t command)
{
    std::vector<Params> all;
    for (const auto &packet : bus.sent)
        if (packet.command == command)
            all.push_back(packet.params);
    return all;
}

std::string written_bytes()
{
    std::string bytes;
    for (const auto &packet : bus.sent)
        if (packet.command == CMD::DBC_SEGMENT_WRITE)
            bytes += packet.payload;
    return bytes;
}

// These tests don't link fujiDevice, so this copies its choice of cartridge or drive.
error_is_true mount_file(drivewireDisk &disk, fnFile *file, const char *filename, uint32_t size,
                         disk_access_flags_t mode)
{
    auto rom = CoCoROMFormats::open<ROMMedia>(filename, file, nullptr);
    if (rom)
        return Cartridge(SYSTEM_BUS).mount(*rom);
    return disk.mount(file, filename, size, mode);
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
    CHECK(file.closes == 1);
}

} // namespace

TEST_CASE("a ROM image goes to the DBC in 512-byte blocks between OPEN and CLOSE, with the mapper layout last")
{
    bus.reset();
    auto image = make_image(CART_8K);
    MemFile file(image);

    drivewireDisk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
         .is_success());
    check_closed_once(disk, file);

    size_t blocks = CART_8K / Cartridge::BLOCK_SIZE;
    std::vector<fujiCommandID_t> expected{CMD::DBC_SEGMENT_OPEN};
    expected.insert(expected.end(), blocks, CMD::DBC_SEGMENT_WRITE);
    expected.insert(expected.end(), {CMD::DBC_MAPPER_SET_INFO, CMD::DBC_MAPPER_ADD_WINDOW,
                                     CMD::DBC_MAPPER_ADD_WINDOW, CMD::DBC_MAPPER_ADD_MIRROR,
                                     CMD::DBC_SEGMENT_CLOSE});
    CHECK(commands() == expected);
    for (const auto &packet : bus.sent)
        CHECK(packet.device == FUJI_DEVICEID::DBC);
    CHECK(bus.sent.front().params == Params{0});
    CHECK(bus.sent[1].payload.size() == Cartridge::BLOCK_SIZE);
    CHECK(bus.sent.back().payload.empty());
    CHECK(written_bytes() == std::string(image.begin(), image.end()));
}

TEST_CASE("a cartridge up to 32K is two fixed windows with the first 16K mirrored down")
{
    for (size_t size : {CART_4K, CART_8K * 4})
    {
        CAPTURE(size);
        bus.reset();
        MemFile file(make_image(size));

        drivewireDisk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, size, DISK_ACCESS_MODE_READ).is_success());
        CHECK(params_of(CMD::DBC_MAPPER_ADD_WINDOW)
              == std::vector<Params>{{0, 0, 0xC000, 0x10000}, {0, 0, 0x10000, 0x14000}});
        CHECK(params_of(CMD::DBC_MAPPER_ADD_MIRROR)
              == std::vector<Params>{{0x8000, 0xC000, 0x10000}});
        check_closed_once(disk, file);
    }
}

TEST_CASE("a cartridge over 32K is one banked GMC window")
{
    for (size_t size : {CART_8K * 8, CART_8K * 16})
    {
        CAPTURE(size);
        bus.reset();
        MemFile file(make_image(size));

        drivewireDisk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, size, DISK_ACCESS_MODE_READ).is_success());
        CHECK(params_of(CMD::DBC_MAPPER_ADD_WINDOW)
              == std::vector<Params>{{0xFF40, 0xFF41, 0xC000, 0x10000}});
        CHECK(params_of(CMD::DBC_MAPPER_ADD_MIRROR)
              == std::vector<Params>{{0x8000, 0xC000, 0xC000}});
        check_closed_once(disk, file);
    }
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
        CHECK(mount_file(disk, &file, filename, image.size(), DISK_ACCESS_MODE_READ).is_success());
        CHECK(written_bytes() == std::string(image.begin(), image.end()));
        check_closed_once(disk, file);
    }
}

TEST_CASE("an empty ROM image fails without touching the bus")
{
    bus.reset();
    MemFile file({});

    drivewireDisk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, 0, DISK_ACCESS_MODE_READ).is_error());
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
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, CART_8K, DISK_ACCESS_MODE_READ).is_error());
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
    REQUIRE(bus.sent.size() == 1);
    CHECK(bus.sent[0].command == CMD::DBC_SEGMENT_OPEN);
}

TEST_CASE("no reply to a block fails the mount but sends an aborting CLOSE")
{
    bus.reset();
    bus.replies = {Reply::Ack, Reply::Ack, Reply::None};
    MemFile file(make_image(CART_8K));

    drivewireDisk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, CART_8K, DISK_ACCESS_MODE_READ).is_error());
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
    REQUIRE(bus.sent.size() == 4);
    CHECK(bus.sent[3].command == CMD::DBC_SEGMENT_CLOSE);
    CHECK(bus.sent[3].payload == std::string(1, '\x01'));
}

TEST_CASE("a second ROM mount sends the new image and closes each handle once")
{
    auto first = make_image(CART_8K);
    auto second = make_image(CART_8K * 2);
    MemFile first_file(first);
    MemFile second_file(second);

    drivewireDisk disk;
    bus.reset();
    REQUIRE(mount_file(disk, &first_file, ROM_TEST_FILE, first.size(), DISK_ACCESS_MODE_READ)
           .is_success());

    bus.reset();
    CHECK(mount_file(disk, &second_file, ROM_TEST_FILE, second.size(), DISK_ACCESS_MODE_READ)
         .is_success());
    CHECK(written_bytes() == std::string(second.begin(), second.end()));
    check_closed_once(disk, second_file);
    CHECK(first_file.closes == 1);
}

TEST_CASE("every cartridge size mounts")
{
    for (size_t size : {CART_4K, CART_8K, CART_8K * 2, CART_8K * 4, CART_8K * 8, CART_8K * 16})
    {
        CAPTURE(size);
        bus.reset();
        auto image = make_image(size);
        MemFile file(image);

        drivewireDisk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, size, DISK_ACCESS_MODE_READ).is_success());
        CHECK(written_bytes() == std::string(image.begin(), image.end()));
        check_closed_once(disk, file);
    }
}

TEST_CASE("a caller-given ROM type still needs a .ccc or .rom")
{
    bus.reset();
    MemFile file(make_image(CART_8K));

    drivewireDisk disk;
    CHECK(mount_file(disk, &file, "cart.dat", CART_8K, DISK_ACCESS_MODE_READ).is_error());
    CHECK(bus.sent.empty());
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
}

TEST_CASE("a size no cartridge has is refused before anything is sent")
{
    size_t sizes[] = {
        CART_4K / 2, CART_4K - 1, CART_4K + 1, CART_8K - 1, CART_8K + 1, CART_8K + 1024,
        CART_8K * 3 / 2, CART_8K * 3, CART_8K * 5, CART_8K * 16 + 1, CART_8K * 32,
    };
    for (size_t size : sizes)
    {
        CAPTURE(size);
        bus.reset();
        MemFile file(make_image(size));

        drivewireDisk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, size, DISK_ACCESS_MODE_READ)
             .is_error());
        CHECK(bus.sent.empty());
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}
