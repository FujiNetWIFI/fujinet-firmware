#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Cartridge.h"
#include "disk.h"
#include "RS232ROMFormats.h"
#include "romMountStubs.h"
#include "fujiHost.h"
#include "hash.h"
#include "string_utils.h"
#include "rom/MSXROMDatabase.h"

namespace
{

// .a52 takes any size, so the transfer tests can pick sizes around the block size
#define ROM_TEST_FILE "rs232_rom_mount_test.a52"
#define ROM_BIN_TEST_FILE "rs232_rom_mount_test.bin"
#define MAP_TEST_FILE "rs232_rom_mount_test.cfg"
#define MAP_TEST_FILE_UPPER "rs232_rom_mount_test.CFG"

// An image of total bytes that starts with header.
std::vector<uint8_t> with_header(std::vector<uint8_t> header, size_t total)
{
    auto image = make_image(total);
    for (size_t idx = 0; idx < header.size() && idx < total; idx++)
        image[idx] = header[idx];
    return image;
}

// An iNES image with prg 16K and chr 8K units after its header, total bytes long.
std::vector<uint8_t> nes_image(uint8_t prg, uint8_t chr, size_t total)
{
    std::vector<uint8_t> header(16, 0);
    memcpy(&header[0], "NES\x1A", 4);
    header[4] = prg;
    header[5] = chr;
    return with_header(header, total);
}

// A ColecoVision banked image whose cartridge header is only in its last 16K bank.
std::vector<uint8_t> coleco_banked_image(size_t total)
{
    auto image = make_image(total);
    image[0] = image[1] = 0;
    image[total - 16_kB] = 0x55;
    image[total - 16_kB + 1] = 0xAA;
    return image;
}

std::vector<uint8_t> a78_header()
{
    std::vector<uint8_t> header(128, 0);
    header[0] = 1;
    memcpy(&header[1], "ATARI7800", 9);
    return header;
}

void add_host_file(const char *path, const std::string &contents)
{
    host_files.files[path] = std::vector<uint8_t>(contents.begin(), contents.end());
}

std::string open_header(uint8_t stream_id, uint32_t size)
{
    struct { uint8_t id; u32le_t size; } __attribute__((packed)) hdr;
    hdr.id = stream_id;
    hdr.size = size;
    return std::string((const char *)&hdr, sizeof(hdr));
}

// Bytes written between the OPEN for stream_id and its CLOSE.
std::string stream_bytes(uint8_t stream_id)
{
    std::string bytes;
    bool open = false;
    for (const auto &packet : bus.sent)
    {
        if (packet.command == CMD::NET_OPEN)
            open = !packet.payload.empty() && (uint8_t)packet.payload[0] == stream_id;
        else if (packet.command == CMD::NET_CLOSE)
            open = false;
        else if (open && packet.command == CMD::NET_WRITE)
            bytes += packet.payload;
    }
    return bytes;
}

std::vector<fujiCommandID_t> commands()
{
    std::vector<fujiCommandID_t> sent;
    for (const auto &packet : bus.sent)
        sent.push_back(packet.command);
    return sent;
}

// These tests don't link fujiDevice, so this copies its choice of cartridge or drive.
error_is_true mount_file(rs232Disk &disk, fnFile *file, const char *filename, uint32_t size,
                         disk_access_flags_t mode, fujiHost *host = nullptr)
{
    auto rom = RS232ROMFormats::open<ROMMedia>(filename, file, host);
    if (rom)
        return Cartridge(SYSTEM_BUS).mount(*rom);
    return disk.mount(file, filename, size, mode, host);
}

// Whatever the outcome, every handle opened for the mount is closed exactly
// once by the time the drive is unmounted.
void check_closed_once(rs232Disk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
    for (const auto &opened : host_files.opened)
    {
        CHECK(opened->closes == 1);
        CHECK_FALSE(opened->usedAfterClose);
    }
}

// A failed mount releases the drive straight away, not at the next unmount().
void check_nothing_mounted(rs232Disk &disk, const MemFile &file)
{
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mount_time() == 0);
    CHECK(file.closes == 1);
    for (const auto &opened : host_files.opened)
        CHECK(opened->closes == 1);
}

void reset()
{
    bus.reset();
    host_files.reset();
    msx_rom_db.reset();
}

// An image of total bytes with the MSX cartridge ID "AB" at offset at.
std::vector<uint8_t> msx_image(size_t total, size_t at = 0)
{
    auto image = make_image(total);
    image[0] = image[1] = 0;
    image[at] = 'A';
    image[at + 1] = 'B';
    return image;
}

using Window = std::vector<uint32_t>;

// The params of each packet sent for command, in order.
std::vector<Window> params_of(fujiCommandID_t command)
{
    std::vector<Window> all;
    for (const auto &packet : bus.sent)
        if (packet.command == command)
            all.push_back(packet.params);
    return all;
}

// The SHA-1 of data as uppercase hex.
std::string sha1_hex(const std::vector<uint8_t> &data)
{
    uint8_t digest[20];
    CHECK(Hash::compute(Hash::Algorithm::SHA1, data.data(), data.size(), digest, sizeof(digest)) == 0);
    return mstr::toHex(digest, sizeof(digest));
}

TEST_CASE("a hash stream fed uneven chunks matches the one-shot hash")
{
    auto data = make_image(1000);
    for (auto algorithm : {Hash::Algorithm::MD5, Hash::Algorithm::SHA1, Hash::Algorithm::SHA256,
                           Hash::Algorithm::SHA512})
    {
        size_t len = Hash::hash_length(algorithm, false);
        uint8_t expected[64], actual[64];
        REQUIRE(Hash::compute(algorithm, data.data(), data.size(), expected, len) == 0);

        Hash::Stream stream(algorithm);
        size_t offset = 0;
        for (size_t chunk : {1, 7, 100, 0, 333, 559})
        {
            REQUIRE(stream.update(data.data() + offset, chunk).is_success());
            offset += chunk;
        }
        REQUIRE(offset == data.size());
        REQUIRE(stream.finish(actual, len).is_success());
        CHECK(std::memcmp(actual, expected, len) == 0);
    }
}

// A database line for image: its SHA-1 in lowercase, a tab, the type, CRLF.
std::string db_line(const std::vector<uint8_t> &image, const char *type)
{
    std::string hash = sha1_hex(image);
    for (char &ch : hash)
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    return hash + "\t" + type + "\r\n";
}

const std::string OTHER_DB_LINES =
    "0000000000000000000000000000000000000000\tASCII8\r\n"
    "\r\n"
    "short\r\n";

// Serves image's entry, listed as type, after some unrelated lines.
void set_db(const std::vector<uint8_t> &image, const char *type)
{
    msx_rom_db.hasDb = true;
    msx_rom_db.text = OTHER_DB_LINES + db_line(image, type);
}

// Mounts image as filename, listed in the database as dbType if given, and
// checks the mapper commands sit after the image
// blocks and before CLOSE, with the image bytes unchanged.
void check_msx_mount(const char *filename, const std::vector<uint8_t> &image,
                     const std::vector<Window> &windows, const std::vector<Window> &mirrors,
                     const char *dbType = nullptr)
{
    reset();
    if (dbType != nullptr)
        set_db(image, dbType);
    MemFile file(image);
    rs232Disk disk;
    CHECK(mount_file(disk, &file, filename, image.size(), DISK_ACCESS_MODE_READ).is_success());
    check_closed_once(disk, file);

    REQUIRE(bus.sent.size() >= 4);
    CHECK(bus.sent.front().command == CMD::DBC_SEGMENT_OPEN);
    CHECK(bus.sent.front().params == Window{0});
    CHECK(bus.sent.back().command == CMD::DBC_SEGMENT_CLOSE);
    CHECK(bus.sent.back().payload.empty());

    size_t blocks = (image.size() + Cartridge::BLOCK_SIZE - 1) / Cartridge::BLOCK_SIZE;
    std::vector<fujiCommandID_t> expected{CMD::DBC_SEGMENT_OPEN};
    expected.insert(expected.end(), blocks, CMD::DBC_SEGMENT_WRITE);
    expected.push_back(CMD::DBC_MAPPER_SET_INFO);
    expected.insert(expected.end(), windows.size(), CMD::DBC_MAPPER_ADD_WINDOW);
    expected.insert(expected.end(), mirrors.size(), CMD::DBC_MAPPER_ADD_MIRROR);
    expected.push_back(CMD::DBC_SEGMENT_CLOSE);
    CHECK(commands() == expected);

    CHECK(bus.sent[1 + blocks].payload.empty());
    CHECK(params_of(CMD::DBC_MAPPER_ADD_WINDOW) == windows);
    CHECK(params_of(CMD::DBC_MAPPER_ADD_MIRROR) == mirrors);

    std::string bytes;
    for (const auto &packet : bus.sent)
        if (packet.command == CMD::DBC_SEGMENT_WRITE)
            bytes += packet.payload;
    CHECK(bytes == std::string(image.begin(), image.end()));
}

} // namespace

void systemBus::addDevice(virtualDevice *, fujiDeviceID_t) {}

TEST_CASE("a ROM image goes to DBC stream 0 in 512-byte blocks between OPEN and CLOSE")
{
    reset();
    auto image = make_image(Cartridge::BLOCK_SIZE * 2 + 88);
    MemFile file(image);

    rs232Disk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    check_closed_once(disk, file);

    REQUIRE(bus.sent.size() == 5);
    for (const auto &packet : bus.sent)
        CHECK(packet.device == FUJI_DEVICEID::DBC);
    CHECK(bus.sent[0].command == CMD::NET_OPEN);
    CHECK(bus.sent[0].params == std::vector<uint32_t>{0});
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    CHECK(bus.sent[1].payload.size() == Cartridge::BLOCK_SIZE);
    CHECK(bus.sent[2].payload.size() == Cartridge::BLOCK_SIZE);
    CHECK(bus.sent[3].payload.size() == 88);
    CHECK(bus.sent[4].command == CMD::NET_CLOSE);
    CHECK(bus.sent[4].payload.empty());
    CHECK(stream_bytes(0) == std::string(image.begin(), image.end()));
}

TEST_CASE("each console's image mounts and goes over unchanged")
{
    struct { const char *filename; std::vector<uint8_t> image; } cases[] = {
        {"cart.rom", with_header({0xA8, 0x01, 0xFE}, 64)},
        {"cart.nes", nes_image(1, 1, 16 + 16_kB + 8_kB)},
        {"CART.A78", with_header(a78_header(), 256)},
        {"cart.st2", with_header({'R', 'C', 'A', '2', 3}, 768)},
        {"cart.int", make_image(8_kB)},
        {"cart.itv", make_image(48_kB)},
        {"cart.a52", make_image(8_kB)},
        {"cart.a52", make_image(40_kB)},
        {"cart.bin", make_image(4_kB)},
        {"cart.a26", make_image(4_kB)},
        {"pp.bin", make_image(8_kB + 3)},
        {"dpc.bin", make_image(10_kB + 255)},
        {"dpcplus.bin", make_image(29_kB)},
        {"3eplus.bin", make_image(3_kB)},
        {"big.bin", make_image(256_kB)},
        {"elf.bin", with_header({0x7F, 'E', 'L', 'F'}, 100)},
        {"ace.bin", with_header({'A', 'C', 'E', '-', 'P', 'C', '0', '0'}, 300)},
        {"cart.sms", make_image(64)},
        {"copier.sms", make_image(16_kB + 512)},
        {"homebrew.sms", make_image(8436)},
        {"banked.sms", make_image(256_kB)},
        {"banked-copier.sms", make_image(128_kB + 512)},
        {"cart.chf", with_header({0x55}, 64)},
        {"cart.col", with_header({0xAA, 0x55}, 8_kB)},
        {"skip-logo.col", with_header({0x55, 0xAA}, 16_kB)},
        {"megacart.col", coleco_banked_image(64_kB)},
        {"cart.rom", with_header({0xAA, 0x55}, 16_kB)},
        {"odd-size.rom", with_header({0xAA, 0x55}, 20_kB)},
        {"megacart.rom", coleco_banked_image(64_kB)},
        {"cart-20k.bin", with_header({0x55, 0xAA}, 20_kB)},
        {"headerless.a78", make_image(32_kB)},
        {"car-header.a52", with_header({'C', 'A', 'R', 'T'}, 8_kB + 16)},
        {"raw.st2", make_image(768)},
        {"raw-1k.st2", make_image(1024)},
        {"big.st2", with_header({'R', 'C', 'A', '2', 12}, 12 * 256)},
    };
    for (auto &entry : cases)
    {
        INFO("filename := " << std::string(entry.filename));
        CAPTURE(entry.image.size());
        reset();
        MemFile file(entry.image);

        rs232Disk disk;
        CHECK(mount_file(disk, &file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ)
              .is_success());
        CHECK(stream_bytes(0) == std::string(entry.image.begin(), entry.image.end()));
        check_closed_once(disk, file);
    }
}

TEST_CASE("an Intellivision .int without a .cfg boots with the default map")
{
    reset();
    auto image = make_image(4_kB);
    MemFile file(image);
    fujiHost host;

    rs232Disk disk;
    CHECK(mount_file(disk, &file, "cart.int", image.size(), DISK_ACCESS_MODE_READ, &host)
          .is_success());
    CHECK(stream_bytes(1).empty());
    CHECK(stream_bytes(0) == std::string(image.begin(), image.end()));
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    check_closed_once(disk, file);
}

TEST_CASE("a .cfg sibling goes to stream 1 before the image")
{
    const std::string map = "[mapping]\n$0000 - $1FFF = $5000\n";
    auto image = make_image(Cartridge::BLOCK_SIZE + 2);

    for (const char *map_file : {MAP_TEST_FILE, MAP_TEST_FILE_UPPER})
    {
        INFO("map_file := " << std::string(map_file));
        reset();
        add_host_file(map_file, map);
        MemFile file(image);
        fujiHost host;

        rs232Disk disk;
        CHECK(mount_file(disk, &file, ROM_BIN_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ, &host)
              .is_success());
        CHECK(host_files.opened.size() == 1);
        check_closed_once(disk, file);

        REQUIRE(bus.sent.size() >= 2);
        CHECK(bus.sent[0].params.empty());
        CHECK(bus.sent[0].payload == open_header(1, map.size()));
        CHECK(bus.sent[3].params == std::vector<uint32_t>{0});
        CHECK(stream_bytes(1) == map);
        CHECK(stream_bytes(0) == std::string(image.begin(), image.end()));
        CHECK(commands() == std::vector<fujiCommandID_t>{
                  CMD::NET_OPEN, CMD::NET_WRITE, CMD::NET_CLOSE,
                  CMD::NET_OPEN, CMD::NET_WRITE, CMD::NET_WRITE, CMD::NET_CLOSE});
    }
}

TEST_CASE("an Intellivision image with a .cfg may be any even size")
{
    for (size_t size : {6_kB, 6_kB + 1})
    {
        CAPTURE(size);
        reset();
        add_host_file(MAP_TEST_FILE, "[mapping]\n");
        auto image = make_image(size);
        MemFile file(image);
        fujiHost host;

        rs232Disk disk;
        bool even = size % 2 == 0;
        CHECK((mount_file(disk, &file, ROM_BIN_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ, &host)
               .is_success() || !even));
        CHECK(stream_bytes(0) == (even ? std::string(image.begin(), image.end()) : ""));
        check_closed_once(disk, file);
    }
}

TEST_CASE("without a host the .cfg sibling is not looked for")
{
    reset();
    add_host_file(MAP_TEST_FILE, "[mapping]\n");
    auto image = make_image(16);
    MemFile file(image);

    rs232Disk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(commands() == std::vector<fujiCommandID_t>{
              CMD::NET_OPEN, CMD::NET_WRITE, CMD::NET_CLOSE});
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    CHECK(host_files.opened.empty());
    check_closed_once(disk, file);
}

TEST_CASE("an empty ROM image mounts raw without touching the bus")
{
    reset();
    MemFile file({});

    rs232Disk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, 0, DISK_ACCESS_MODE_READ).is_success());
    CHECK(bus.sent.empty());
    CHECK(disk.device_active);
    check_closed_once(disk, file);
}

TEST_CASE("a refused or unanswered OPEN fails the mount and sends nothing more")
{
    auto image = make_image(Cartridge::BLOCK_SIZE);
    for (Reply reply : {Reply::Nak, Reply::None})
    {
        CAPTURE((int)reply);
        reset();
        bus.replies = {reply};
        MemFile file(image);

        rs232Disk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              .is_error());
        CHECK(commands() == std::vector<fujiCommandID_t>{CMD::NET_OPEN});
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a refused or unanswered block sends an aborting CLOSE")
{
    auto image = make_image(Cartridge::BLOCK_SIZE * 3);
    for (Reply reply : {Reply::Nak, Reply::None})
    {
        CAPTURE((int)reply);
        reset();
        bus.replies = {Reply::Ack, Reply::Ack, reply};
        MemFile file(image);

        rs232Disk disk;
        CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              .is_error());
        REQUIRE(bus.sent.size() == 4);
        CHECK(bus.sent[3].command == CMD::NET_CLOSE);
        CHECK(bus.sent[3].payload == std::string(1, '\x01'));
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a refused CLOSE fails the mount")
{
    reset();
    bus.replies = {Reply::Ack, Reply::Ack, Reply::Nak};
    auto image = make_image(16);
    MemFile file(image);

    rs232Disk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          .is_error());
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
}

TEST_CASE("a failed memory map push leaves the image unsent")
{
    reset();
    bus.replies = {Reply::Nak};
    add_host_file(MAP_TEST_FILE, "[mapping]\n");
    auto image = make_image(16);
    MemFile file(image);
    fujiHost host;

    rs232Disk disk;
    CHECK(mount_file(disk, &file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ, &host)
          .is_error());
    CHECK(commands() == std::vector<fujiCommandID_t>{CMD::NET_OPEN});
    CHECK(bus.sent[0].payload == open_header(1, 10));
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
}

TEST_CASE("a second ROM mount sends the new image and closes each handle once")
{
    auto first = make_image(100);
    auto second = make_image(300);
    MemFile first_file(first);
    MemFile second_file(second);

    rs232Disk disk;
    reset();
    REQUIRE(mount_file(disk, &first_file, ROM_TEST_FILE, first.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    reset();
    CHECK(mount_file(disk, &second_file, ROM_TEST_FILE, second.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(stream_bytes(0) == std::string(second.begin(), second.end()));
    check_closed_once(disk, second_file);
    CHECK(first_file.closes == 1);
}

TEST_CASE("an image its extension's format doesn't recognize mounts raw")
{
    struct { const char *why; const char *filename; std::vector<uint8_t> image; } cases[] = {
        {"Intellicart magic", "cart.rom", with_header({0xA9, 0x01, 0xFE}, 64)},
        {"Intellicart check byte", "cart.rom", with_header({0xA8, 0x01, 0xFF}, 64)},
        {"iNES magic", "cart.nes", with_header({'N', 'E', 'S', 0x00}, 64)},
        {"iNES shorter than its PRG and CHR", "cart.nes", nes_image(2, 1, 16 + 16_kB)},
        {"iNES with no PRG", "cart.nes", nes_image(0, 1, 16 + 8_kB)},
        {"Channel F signature", "cart.chf", make_image(64)},
        {"ColecoVision header", "cart.col", make_image(8_kB)},
        {"ColecoVision banked header", "cart.col", make_image(64_kB)},
        {"ColecoVision header on a .rom", "cart.rom", make_image(20_kB)},
        {".a78 header with nothing after it", "cart.a78", a78_header()},
        {".car header alone", "cart.a52", with_header({'C', 'A', 'R', 'T'}, 16)},
        {"copier header with nothing after it", "cart.sms", make_image(512)},
        {".st2 under two blocks", "cart.st2", with_header({'R', 'C', 'A', '2', 1}, 768)},
        {".st2 short of its blocks", "cart.st2", with_header({'R', 'C', 'A', '2', 4}, 768)},
        {"empty .a78", "cart.a78", {}},
        {"headerless .a78 not in 16K banks", "cart.a78", make_image(8_kB)},
        {"empty .a52", "cart.a52", {}},
        {"empty .st2", "cart.st2", {}},
        {"headerless .st2 over 1K", "cart.st2", make_image(1025)},
        {"empty .bin", "cart.bin", {}},
        {"2600 size", "cart.bin", make_image(4_kB + 1)},
        {"2600 size past DPC", "cart.bin", make_image(10_kB + 257)},
        {"2600 size past 3E+", "cart.bin", make_image(65_kB)},
        {"empty .sms", "cart.sms", {}},
        {".sms overdump", "cart.sms", make_image(256_kB + 64)},
        {".sms just over one page", "cart.sms", make_image(16_kB + 100)},
        {"empty .chf", "cart.chf", {}},
        {"empty .col", "cart.col", {}},
        {"empty .int", "cart.int", {}},
        {".int with no default map", "cart.int", make_image(6_kB)},
        {".int of an odd size", "cart.int", make_image(4_kB + 1)},
    };
    for (auto &entry : cases)
    {
        INFO("why := " << std::string(entry.why));
        reset();
        MemFile file(entry.image);

        rs232Disk disk;
        CHECK(mount_file(disk, &file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ)
              .is_success());
        CHECK(bus.sent.empty());
        CHECK(disk.device_active);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a 5200, 7800, SMS, or ColecoVision image sends the .cfg beside it")
{
    const std::string map = "mapper=test\n";
    struct { const char *filename; std::vector<uint8_t> image; } cases[] = {
        {"cart.a52", make_image(8_kB)},
        {"cart.a78", with_header(a78_header(), 256)},
        {"cart.sms", make_image(64)},
        {"cart.col", with_header({0xAA, 0x55}, 8_kB)},
    };
    for (auto &entry : cases)
    {
        INFO("filename := " << std::string(entry.filename));
        reset();
        add_host_file("cart.cfg", map);
        MemFile file(entry.image);
        fujiHost host;

        rs232Disk disk;
        CHECK(mount_file(disk, &file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ,
                         &host).is_success());
        REQUIRE(!bus.sent.empty());
        CHECK(bus.sent[0].payload == open_header(1, map.size()));
        CHECK(stream_bytes(1) == map);
        CHECK(stream_bytes(0) == std::string(entry.image.begin(), entry.image.end()));
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}

TEST_CASE("other consoles' images don't send the .cfg beside them")
{
    struct { const char *filename; std::vector<uint8_t> image; } cases[] = {
        {"cart.rom", with_header({0xA8, 0x01, 0xFE}, 64)},
        {"cart.nes", nes_image(1, 1, 16 + 16_kB + 8_kB)},
        {"cart.st2", with_header({'R', 'C', 'A', '2', 3}, 768)},
        {"cart.chf", with_header({0x55}, 64)},
    };
    for (auto &entry : cases)
    {
        INFO("filename := " << std::string(entry.filename));
        reset();
        add_host_file("cart.cfg", "mapper=test\n");
        MemFile file(entry.image);
        fujiHost host;

        rs232Disk disk;
        CHECK(mount_file(disk, &file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ,
                         &host).is_success());
        CHECK(stream_bytes(1).empty());
        CHECK(stream_bytes(0) == std::string(entry.image.begin(), entry.image.end()));
        CHECK(host_files.opened.empty());
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}

TEST_CASE("an untagged 16K MSX ROM is one fixed window at 0x4000")
{
    check_msx_mount("game.rom", msx_image(16_kB), {{0, 0, 0x4000, 0x8000}}, {});
}

TEST_CASE("an untagged 32K MSX ROM is two fixed windows from 0x4000")
{
    check_msx_mount("game.mx1", msx_image(32_kB),
                    {{0, 0, 0x4000, 0x8000}, {0, 0, 0x8000, 0xC000}}, {});
}

TEST_CASE("an untagged 48K MSX ROM with its ID at 0x4000 gets four windows from 0")
{
    std::vector<Window> windows = {{0, 0, 0x0000, 0x4000}, {0, 0, 0x4000, 0x8000},
                                   {0, 0, 0x8000, 0xC000}, {0, 0, 0xC000, 0x10000}};
    check_msx_mount("game.rom", msx_image(48_kB, 0x4000), windows, {});
    check_msx_mount("game.mx2", msx_image(64_kB, 0x4000), windows, {});
}

TEST_CASE("a [KonamiSCC] tag sets the SCC windows and mirrors")
{
    check_msx_mount("King's Valley 2 [KonamiSCC].rom", msx_image(128_kB),
                    {{0x5000, 0x5800, 0x4000, 0x6000}, {0x7000, 0x7800, 0x6000, 0x8000},
                     {0x9000, 0x9800, 0x8000, 0xA000}, {0xB000, 0xB800, 0xA000, 0xC000}},
                    {{0x0000, 0x4000, 0x8000}, {0xC000, 0x10000, 0x4000}});
}

TEST_CASE("mapper tags are case-insensitive and pick their own layouts")
{
    check_msx_mount("game [aScII8].rom", msx_image(64_kB),
                    {{0x6000, 0x6800, 0x4000, 0x6000}, {0x6800, 0x7000, 0x6000, 0x8000},
                     {0x7000, 0x7800, 0x8000, 0xA000}, {0x7800, 0x8000, 0xA000, 0xC000}}, {});
    check_msx_mount("game [ASCII16].ROM", msx_image(64_kB),
                    {{0x6000, 0x6800, 0x4000, 0x8000}, {0x7000, 0x7800, 0x8000, 0xC000}}, {});
    check_msx_mount("game [konami].rom", msx_image(64_kB),
                    {{0, 0, 0x4000, 0x6000}, {0x6000, 0x8000, 0x6000, 0x8000},
                     {0x8000, 0xA000, 0x8000, 0xA000}, {0xA000, 0xC000, 0xA000, 0xC000}},
                    {{0x0000, 0x4000, 0x4000}, {0xC000, 0x10000, 0x8000}});
}

TEST_CASE("an untagged MSX ROM listed as KonamiSCC gets the SCC layout")
{
    check_msx_mount("King's Valley 2.rom", msx_image(128_kB),
                    {{0x5000, 0x5800, 0x4000, 0x6000}, {0x7000, 0x7800, 0x6000, 0x8000},
                     {0x9000, 0x9800, 0x8000, 0xA000}, {0xB000, 0xB800, 0xA000, 0xC000}},
                    {{0x0000, 0x4000, 0x8000}, {0xC000, 0x10000, 0x4000}}, "KonamiSCC");
}

TEST_CASE("a filename tag overrides the database")
{
    check_msx_mount("game [ASCII16].rom", msx_image(64_kB),
                    {{0x6000, 0x6800, 0x4000, 0x8000}, {0x7000, 0x7800, 0x8000, 0xC000}}, {},
                    "Konami");
}

TEST_CASE("database types pick ASCII8, ASCII16, and Konami layouts")
{
    check_msx_mount("a.rom", msx_image(64_kB),
                    {{0x6000, 0x6800, 0x4000, 0x6000}, {0x6800, 0x7000, 0x6000, 0x8000},
                     {0x7000, 0x7800, 0x8000, 0xA000}, {0x7800, 0x8000, 0xA000, 0xC000}}, {},
                    "ASCII8");
    check_msx_mount("a.rom", msx_image(64_kB),
                    {{0x6000, 0x6800, 0x4000, 0x8000}, {0x7000, 0x7800, 0x8000, 0xC000}}, {},
                    "ASCII16");
    check_msx_mount("a.rom", msx_image(64_kB),
                    {{0, 0, 0x4000, 0x6000}, {0x6000, 0x8000, 0x6000, 0x8000},
                     {0x8000, 0xA000, 0x8000, 0xA000}, {0xA000, 0xC000, 0xA000, 0xC000}},
                    {{0x0000, 0x4000, 0x4000}, {0xC000, 0x10000, 0x8000}}, "Konami");
}

TEST_CASE("database entries that place a plain ROM at a page")
{
    check_msx_mount("a.rom", msx_image(16_kB), {{0, 0, 0x8000, 0xC000}}, {}, "Page2");
    check_msx_mount("a.rom", msx_image(32_kB),
                    {{0, 0, 0x8000, 0xC000}, {0, 0, 0xC000, 0x10000}}, {}, "0x8000");
    check_msx_mount("a.rom", msx_image(32_kB),
                    {{0, 0, 0x0000, 0x4000}, {0, 0, 0x4000, 0x8000}}, {}, "0x0000");
    check_msx_mount("a.rom", msx_image(48_kB),
                    {{0, 0, 0x0000, 0x4000}, {0, 0, 0x4000, 0x8000},
                     {0, 0, 0x8000, 0xC000}, {0, 0, 0xC000, 0x10000}}, {}, "0x0000");
}

TEST_CASE("database types that need no mapper keep the plain layout")
{
    check_msx_mount("a.rom", msx_image(16_kB), {{0, 0, 0x4000, 0x8000}}, {}, "Mirrored");
    check_msx_mount("a.rom", msx_image(32_kB),
                    {{0, 0, 0x4000, 0x8000}, {0, 0, 0x8000, 0xC000}}, {}, "GameMaster2");
    check_msx_mount("a.rom", msx_image(32_kB),
                    {{0, 0, 0x4000, 0x8000}, {0, 0, 0x8000, 0xC000}}, {}, "ascii8");
}

TEST_CASE("no database, or an image it doesn't list, keeps the plain layout")
{
    check_msx_mount("a.rom", msx_image(16_kB), {{0, 0, 0x4000, 0x8000}}, {});

    auto image = msx_image(16_kB);
    auto other = msx_image(32_kB);
    reset();
    set_db(other, "Page2");
    MemFile file(image);
    rs232Disk disk;
    CHECK(mount_file(disk, &file, "a.rom", image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(params_of(CMD::DBC_MAPPER_ADD_WINDOW) == std::vector<Window>{{0, 0, 0x4000, 0x8000}});
}

static std::optional<std::string> lookup_in(const std::string &text, const std::string &hash)
{
    FILE *db = tmpfile();
    REQUIRE(db != nullptr);
    fwrite(text.data(), 1, text.size(), db);
    rewind(db);
    auto type = msx_rom_db_lookup(db, hash);
    fclose(db);
    return type;
}

TEST_CASE("the database lookup ignores case and line endings")
{
    const std::string hash = "0123456789ABCDEF0123456789ABCDEF01234567";
    const std::string lower = "0123456789abcdef0123456789abcdef01234567";
    CHECK(lookup_in(lower + "\tKonami\r\n", hash) == "Konami");
    CHECK(lookup_in(hash + "\tKonami\n", lower) == "Konami");
    CHECK(lookup_in(hash + "\tKonami", hash) == "Konami");
    CHECK(lookup_in("x\n" + hash + "\tASCII8  \r\n", hash) == "ASCII8");
}

TEST_CASE("the database lookup skips malformed lines")
{
    const std::string hash = "0123456789ABCDEF0123456789ABCDEF01234567";
    CHECK_FALSE(lookup_in("", hash).has_value());
    CHECK_FALSE(lookup_in(hash + " Konami\n", hash).has_value());
    CHECK_FALSE(lookup_in(hash + "\t\n", hash).has_value());
    CHECK_FALSE(lookup_in("\n\r\nabc\n", hash).has_value());
    CHECK_FALSE(lookup_in(hash + "\tKonami\n", "0123").has_value());
    CHECK(lookup_in("\n" + hash.substr(0, 39) + "\n" + hash + "\tKonami\n", hash) == "Konami");
}

TEST_CASE("an overlong database line is discarded without desyncing")
{
    const std::string hash = "0123456789ABCDEF0123456789ABCDEF01234567";
    // The overlong line has the wanted hash and a tab, but isn't a valid entry;
    // its tail looks like one.
    std::string text = hash + "\t" + std::string(300, 'x') + "\n";
    text += std::string(100, 'y') + hash + "\tASCII16\n";
    CHECK_FALSE(lookup_in(text, hash).has_value());
    CHECK(lookup_in(text + hash + "\tKonami\n", hash) == "Konami");
    CHECK(lookup_in(std::string(127, 'z') + "\n" + hash + "\tKonami\n", hash) == "Konami");
}

TEST_CASE("a .rom without the MSX cartridge ID is not taken as MSX")
{
    reset();
    auto image = make_image(16_kB);
    MemFile file(image);
    rs232Disk disk;
    CHECK(mount_file(disk, &file, "game [KonamiSCC].rom", image.size(), DISK_ACCESS_MODE_READ)
          .is_success());
    CHECK(params_of(CMD::DBC_MAPPER_ADD_WINDOW).empty());
    CHECK(params_of(CMD::DBC_MAPPER_SET_INFO).empty());
    check_closed_once(disk, file);
}

TEST_CASE("a refused mapper window sends an aborting CLOSE and fails the mount")
{
    auto image = msx_image(16_kB);
    reset();
    size_t blocks = 16_kB / Cartridge::BLOCK_SIZE;
    // OPEN, the blocks, SET_INFO, then the window
    bus.replies.assign(1 + blocks + 1, Reply::Ack);
    bus.replies.push_back(Reply::Nak);
    MemFile file(image);

    rs232Disk disk;
    CHECK(mount_file(disk, &file, "game.rom", image.size(), DISK_ACCESS_MODE_READ).is_error());
    REQUIRE(bus.sent.size() == blocks + 4);
    CHECK(bus.sent.back().command == CMD::DBC_SEGMENT_CLOSE);
    CHECK(bus.sent.back().payload == std::string(1, '\x01'));
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
}
