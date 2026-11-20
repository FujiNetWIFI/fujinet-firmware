#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "iec/drive.h"
#include "cbm_defines.h"
#include "BlankImage.h"
#include "romMountStubs.h"

namespace
{

#define D64_TEST_FILE "iec_disk_test.d64"

constexpr uint8_t DEVICE_NUMBER = 8;
constexpr uint8_t LOAD_CHANNEL = 0;
constexpr uint8_t SAVE_CHANNEL = 1;
constexpr size_t SECTOR_SIZE = 256;
constexpr size_t D64_SIZE = 174848;
constexpr uint8_t DIRECTORY_TRACK = 18;
constexpr uint8_t FILE_TRACK = 17;
constexpr uint8_t PRG_CLOSED = 0x82;
constexpr uint8_t SHIFTED_SPACE = 0xA0;

// Reaches the channel handlers the bus calls, which are the only way in to a
// file on the mounted image.
class TestDrive : public iecDrive
{
public:
    TestDrive() : iecDrive(DEVICE_NUMBER) {}
    using iecDrive::open;
    using iecDrive::read;
    using iecDrive::write;
    using iecDrive::close;
};

// Byte offset of a track and sector: 21 sectors a track up to 17, 19 to 24,
// 18 to 30, and 17 after.
size_t d64_offset(uint8_t track, uint8_t sector)
{
    size_t block = 0;
    for (uint8_t prior = 1; prior < track; prior++)
        block += prior <= 17 ? 21 : prior <= 24 ? 19 : prior <= 30 ? 18 : 17;
    return (block + sector) * SECTOR_SIZE;
}

struct D64File
{
    const char *name;
    std::vector<uint8_t> contents;
};

// A formatted D64 holding PRG files, the first written from track 17 sector 0
// on, the next from track 16, and so on, in 254-byte pieces that each link to
// the next.
std::vector<uint8_t> d64_with_files(const std::vector<D64File> &files)
{
    MemFile blank(std::vector<uint8_t>{});
    REQUIRE(BlankImage::create(&blank, D64_TEST_FILE, SECTOR_SIZE, D64_SIZE / SECTOR_SIZE)
                .is_success());
    std::vector<uint8_t> image = blank.contents();
    REQUIRE(image.size() == D64_SIZE);

    for (size_t idx = 0; idx < files.size(); idx++)
    {
        const D64File &file = files[idx];
        uint8_t track = (uint8_t)(FILE_TRACK - idx);
        size_t pieces = (file.contents.size() + 253) / 254;
        for (size_t piece = 0; piece < pieces; piece++)
        {
            uint8_t *sector = image.data() + d64_offset(track, (uint8_t)piece);
            size_t start = piece * 254;
            size_t length = std::min<size_t>(254, file.contents.size() - start);
            bool last = piece + 1 == pieces;
            sector[0] = last ? 0 : track;
            sector[1] = last ? (uint8_t)(length + 1) : (uint8_t)(piece + 1);
            memcpy(sector + 2, file.contents.data() + start, length);
        }

        uint8_t *entry = image.data() + d64_offset(DIRECTORY_TRACK, 1) + idx * 32;
        entry[2] = PRG_CLOSED;
        entry[3] = track;
        entry[4] = 0;
        memset(entry + 5, SHIFTED_SPACE, 16);
        memcpy(entry + 5, file.name, strlen(file.name));
        entry[30] = (uint8_t)pieces;
        entry[31] = 0;
    }
    return image;
}

// Reads the open channel to its end, as a LOAD does.
std::vector<uint8_t> read_all(TestDrive &drive, uint8_t channel)
{
    std::vector<uint8_t> data;
    uint8_t buffer[64];
    bool eoi = false;
    while (!eoi)
    {
        uint8_t got = drive.read(channel, buffer, sizeof(buffer), &eoi);
        if (got == 0)
            break;
        data.insert(data.end(), buffer, buffer + got);
    }
    return data;
}

bool contains(const std::vector<uint8_t> &data, const std::string &text)
{
    return std::search(data.begin(), data.end(), text.begin(), text.end()) != data.end();
}

void check_closed_once(TestDrive &drive, const MemFile &file)
{
    drive.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

} // namespace

TEST_CASE("each file on a mounted D64 reads back as stored")
{
    auto hello = make_image(600);
    auto world = make_image(100);
    auto image = d64_with_files({{"HELLO", hello}, {"WORLD", world}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    for (int pass = 0; pass < 2; pass++)
    {
        CAPTURE(pass);
        REQUIRE(drive.open(LOAD_CHANNEL, "WORLD"));
        CHECK(drive.getStatusCode() == ST_OK);
        CHECK(read_all(drive, LOAD_CHANNEL) == world);
        drive.close(LOAD_CHANNEL);

        REQUIRE(drive.open(LOAD_CHANNEL, "HELLO"));
        CHECK(read_all(drive, LOAD_CHANNEL) == hello);
        drive.close(LOAD_CHANNEL);
    }
    check_closed_once(drive, file);
}

TEST_CASE("the directory lists each file on the D64")
{
    auto image = d64_with_files({{"HELLO", make_image(600)}, {"WORLD", make_image(100)}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    REQUIRE(drive.open(LOAD_CHANNEL, "$"));
    auto listing = read_all(drive, LOAD_CHANNEL);
    drive.close(LOAD_CHANNEL);
    CHECK(contains(listing, "\"HELLO\""));
    CHECK(contains(listing, "\"WORLD\""));
    CHECK(contains(listing, "PRG"));
    check_closed_once(drive, file);
}

TEST_CASE("a file not on the D64 isn't found")
{
    auto image = d64_with_files({{"HELLO", make_image(600)}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    CHECK_FALSE(drive.open(LOAD_CHANNEL, "MISSING"));
    CHECK(drive.getStatusCode() == ST_FILE_NOT_FOUND);
    check_closed_once(drive, file);
}

TEST_CASE("a wildcard name opens the first matching file on the D64")
{
    auto hello = make_image(600);
    auto image = d64_with_files({{"HELLO", hello}, {"WORLD", make_image(100)}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    REQUIRE(drive.open(LOAD_CHANNEL, "HEL*"));
    CHECK(drive.getStatusCode() == ST_OK);
    CHECK(read_all(drive, LOAD_CHANNEL) == hello);
    drive.close(LOAD_CHANNEL);
    check_closed_once(drive, file);
}

TEST_CASE("saving to a file on a D64 is refused as write-protected")
{
    auto image = d64_with_files({{"HELLO", make_image(600)}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
            .is_success());

    CHECK_FALSE(drive.open(SAVE_CHANNEL, "NEWFILE"));
    CHECK(drive.getStatusCode() == ST_WRITE_PROTECT_ON);
    CHECK_FALSE(drive.open(SAVE_CHANNEL, "@:HELLO"));
    CHECK(drive.getStatusCode() == ST_WRITE_PROTECT_ON);
    CHECK(file.contents() == image);
    check_closed_once(drive, file);
}

TEST_CASE("mounting over a disk closes the old image once")
{
    auto first_image = d64_with_files({{"FIRST", make_image(300)}});
    auto second_contents = make_image(400);
    auto second_image = d64_with_files({{"SECOND", second_contents}});
    MemFile first(first_image);
    MemFile second(second_image);
    TestDrive drive;

    REQUIRE(drive.mount(&first, D64_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    REQUIRE(drive.mount(&second, D64_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
            .is_success());
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);

    REQUIRE(drive.open(LOAD_CHANNEL, "SECOND"));
    CHECK(read_all(drive, LOAD_CHANNEL) == second_contents);
    drive.close(LOAD_CHANNEL);
    CHECK_FALSE(drive.open(LOAD_CHANNEL, "FIRST"));
    check_closed_once(drive, second);
}

TEST_CASE("unmount closes the image once and its files are gone")
{
    auto image = d64_with_files({{"HELLO", make_image(600)}});
    MemFile file(image);
    TestDrive drive;
    REQUIRE(drive.mount(&file, D64_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
            .is_success());

    check_closed_once(drive, file);
    CHECK_FALSE(drive.open(LOAD_CHANNEL, "HELLO"));
    CHECK(drive.getStatusCode() == ST_FILE_NOT_FOUND);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}
