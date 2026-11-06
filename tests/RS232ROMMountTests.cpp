#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"
#include "fujiHost.h"

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
    CHECK(disk.disktype() == MEDIATYPE_UNKNOWN);
    CHECK(disk.mount_time() == 0);
    CHECK(file.closes == 1);
    for (const auto &opened : host_files.opened)
        CHECK(opened->closes == 1);
}

void reset()
{
    bus.reset();
    host_files.reset();
}

} // namespace

void systemBus::addDevice(virtualDevice *, fujiDeviceID_t) {}

TEST_CASE("a ROM image goes to DBC stream 0 in 512-byte blocks between OPEN and CLOSE")
{
    reset();
    auto image = make_image(DISK_SECTORBUF_SIZE * 2 + 88);
    MemFile file(image);

    rs232Disk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_ROM);
    check_closed_once(disk, file);

    REQUIRE(bus.sent.size() == 5);
    for (const auto &packet : bus.sent)
        CHECK(packet.device == FUJI_DEVICEID::DBC);
    CHECK(bus.sent[0].command == CMD::NET_OPEN);
    CHECK(bus.sent[0].params == std::vector<uint32_t>{0});
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    CHECK(bus.sent[1].payload.size() == DISK_SECTORBUF_SIZE);
    CHECK(bus.sent[2].payload.size() == DISK_SECTORBUF_SIZE);
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
        CHECK(disk.mount(&file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ)
              == MEDIATYPE_ROM);
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
    CHECK(disk.mount(&file, "cart.int", image.size(), DISK_ACCESS_MODE_READ,
                     MEDIATYPE_UNKNOWN, &host) == MEDIATYPE_ROM);
    CHECK(stream_bytes(1).empty());
    CHECK(stream_bytes(0) == std::string(image.begin(), image.end()));
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    check_closed_once(disk, file);
}

TEST_CASE("a .cfg sibling goes to stream 1 before the image")
{
    const std::string map = "[mapping]\n$0000 - $1FFF = $5000\n";
    auto image = make_image(DISK_SECTORBUF_SIZE + 2);

    for (const char *map_file : {MAP_TEST_FILE, MAP_TEST_FILE_UPPER})
    {
        INFO("map_file := " << std::string(map_file));
        reset();
        add_host_file(map_file, map);
        MemFile file(image);
        fujiHost host;

        rs232Disk disk;
        CHECK(disk.mount(&file, ROM_BIN_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ,
                         MEDIATYPE_UNKNOWN, &host) == MEDIATYPE_ROM);
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
        mediatype_t expected = even ? MEDIATYPE_ROM : MEDIATYPE_UNKNOWN;
        CHECK(disk.mount(&file, ROM_BIN_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ,
                         MEDIATYPE_UNKNOWN, &host) == expected);
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
    CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_ROM);
    CHECK(commands() == std::vector<fujiCommandID_t>{
              CMD::NET_OPEN, CMD::NET_WRITE, CMD::NET_CLOSE});
    CHECK(bus.sent[0].payload == open_header(0, image.size()));
    CHECK(host_files.opened.empty());
    check_closed_once(disk, file);
}

TEST_CASE("an empty ROM image fails without touching the bus")
{
    reset();
    MemFile file({});

    rs232Disk disk;
    CHECK(disk.mount(&file, ROM_TEST_FILE, 0, DISK_ACCESS_MODE_READ) == MEDIATYPE_UNKNOWN);
    CHECK(bus.sent.empty());
    check_nothing_mounted(disk, file);
    check_closed_once(disk, file);
}

TEST_CASE("a refused or unanswered OPEN fails the mount and sends nothing more")
{
    auto image = make_image(DISK_SECTORBUF_SIZE);
    for (Reply reply : {Reply::Nak, Reply::None})
    {
        CAPTURE((int)reply);
        reset();
        bus.replies = {reply};
        MemFile file(image);

        rs232Disk disk;
        CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              == MEDIATYPE_UNKNOWN);
        CHECK(commands() == std::vector<fujiCommandID_t>{CMD::NET_OPEN});
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a refused or unanswered block sends an aborting CLOSE")
{
    auto image = make_image(DISK_SECTORBUF_SIZE * 3);
    for (Reply reply : {Reply::Nak, Reply::None})
    {
        CAPTURE((int)reply);
        reset();
        bus.replies = {Reply::Ack, Reply::Ack, reply};
        MemFile file(image);

        rs232Disk disk;
        CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
              == MEDIATYPE_UNKNOWN);
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
    CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_UNKNOWN);
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
    CHECK(disk.mount(&file, ROM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ,
                     MEDIATYPE_UNKNOWN, &host) == MEDIATYPE_UNKNOWN);
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
    REQUIRE(disk.mount(&first_file, ROM_TEST_FILE, first.size(), DISK_ACCESS_MODE_READ)
            == MEDIATYPE_ROM);

    reset();
    CHECK(disk.mount(&second_file, ROM_TEST_FILE, second.size(), DISK_ACCESS_MODE_READ)
          == MEDIATYPE_ROM);
    CHECK(stream_bytes(0) == std::string(second.begin(), second.end()));
    check_closed_once(disk, second_file);
    CHECK(first_file.closes == 1);
}

TEST_CASE("an image its extension's format doesn't recognize is refused")
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
        {"unknown extension", "cart.dat", make_image(64)},
    };
    for (auto &entry : cases)
    {
        INFO("why := " << std::string(entry.why));
        reset();
        MemFile file(entry.image);

        rs232Disk disk;
        CHECK(disk.mount(&file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ,
                         MEDIATYPE_ROM) == MEDIATYPE_UNKNOWN);
        CHECK(bus.sent.empty());
        check_nothing_mounted(disk, file);
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
        CHECK(disk.mount(&file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ,
                         MEDIATYPE_UNKNOWN, &host) == MEDIATYPE_ROM);
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
        CHECK(disk.mount(&file, entry.filename, entry.image.size(), DISK_ACCESS_MODE_READ,
                         MEDIATYPE_UNKNOWN, &host) == MEDIATYPE_ROM);
        CHECK(stream_bytes(1).empty());
        CHECK(stream_bytes(0) == std::string(entry.image.begin(), entry.image.end()));
        CHECK(host_files.opened.empty());
        check_nothing_mounted(disk, file);
        check_closed_once(disk, file);
    }
}
