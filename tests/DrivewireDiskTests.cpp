#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "disk.h"
#include "romMountStubs.h"

namespace
{

#define DSK_TEST_FILE "drivewire_disk_test.dsk"
#define VDK_TEST_FILE "drivewire_disk_test.vdk"
#define MRM_TEST_FILE "drivewire_disk_test.mrm"
#define CAS_TEST_FILE "tape.cas"

constexpr size_t VDK_HEADER_SIZE = 12;
constexpr size_t MRM_BLOCK = 32_kB;
constexpr uint8_t NO_MEDIA_STATUS = 3;
constexpr uint8_t EOF_STATUS = 2;

// Every sector holds different bytes, so a read from the wrong place shows.
std::vector<uint8_t> numbered_sectors(size_t count, size_t header = 0)
{
    std::vector<uint8_t> image(header + count * MEDIA_BLOCK_SIZE, 0xEE);
    for (size_t sector = 0; sector < count; sector++)
        for (size_t offset = 0; offset < MEDIA_BLOCK_SIZE; offset++)
            image[header + sector * MEDIA_BLOCK_SIZE + offset] =
                (uint8_t)(sector * 3 + offset + 1);
    return image;
}

// A VDK: the signature, a 12-byte header, and no compression.
std::vector<uint8_t> vdk_sectors(size_t count)
{
    auto image = numbered_sectors(count, VDK_HEADER_SIZE);
    image[0] = 'd';
    image[1] = 'k';
    image[2] = VDK_HEADER_SIZE;
    image[3] = 0;
    image[11] = 0;
    return image;
}

std::vector<uint8_t> slice(const std::vector<uint8_t> &image, size_t start, size_t length)
{
    return std::vector<uint8_t>(image.begin() + start, image.begin() + start + length);
}

std::vector<uint8_t> sector_of(const std::vector<uint8_t> &image, size_t sector,
                               size_t header = 0)
{
    return slice(image, header + sector * MEDIA_BLOCK_SIZE, MEDIA_BLOCK_SIZE);
}

// The CoCo's read: the bus sends whatever the media's own buffer holds.
std::optional<std::vector<uint8_t>> coco_read(drivewireDisk &disk, uint32_t lsn)
{
    uint8_t *buffer = nullptr;
    uint16_t size = 0;
    disk.get_media_buffer(&buffer, &size);
    if (buffer == nullptr || disk.read(lsn, nullptr).is_error())
        return std::nullopt;
    return std::vector<uint8_t>(buffer, buffer + size);
}

// The Dragon's read: the sector is copied into the bus's buffer.
std::optional<std::vector<uint8_t>> dragon_read(drivewireDisk &disk, uint32_t lsn)
{
    std::vector<uint8_t> sector(MEDIA_BLOCK_SIZE);
    if (disk.read(lsn, sector.data()).is_error())
        return std::nullopt;
    return sector;
}

error_is_true write_filled(drivewireDisk &disk, uint32_t lsn, uint8_t value)
{
    std::vector<uint8_t> sector(MEDIA_BLOCK_SIZE, value);
    return disk.write(lsn, sector.data());
}

void check_closed_once(drivewireDisk &disk, const MemFile &file)
{
    disk.unmount();
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
}

// One cassette block: leader, sync, type, length, data, checksum, and a
// trailing leader byte.
void add_cas_block(std::vector<uint8_t> &tape, uint8_t type, const std::vector<uint8_t> &data)
{
    tape.insert(tape.end(), 16, CAS_LEADER_BYTE);
    tape.push_back(CAS_SYNC_BYTE);
    tape.push_back(type);
    tape.push_back((uint8_t)data.size());
    unsigned sum = type + data.size();
    for (uint8_t byte : data)
    {
        tape.push_back(byte);
        sum += byte;
    }
    tape.push_back((uint8_t)sum);
    tape.push_back(CAS_LEADER_BYTE);
}

// A tape holding one ASCII data file, in blocks of at most 255 bytes.
std::vector<uint8_t> cas_with_file(const char *name, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> namefile(15, 0x00);
    memset(namefile.data(), ' ', 8);
    memcpy(namefile.data(), name, strlen(name));
    namefile[8] = 1;    // data
    namefile[9] = 0xFF; // ASCII

    std::vector<uint8_t> tape;
    add_cas_block(tape, CAS_BLOCK_HEADER, namefile);
    for (size_t start = 0; start < payload.size(); start += 255)
    {
        size_t length = std::min<size_t>(255, payload.size() - start);
        add_cas_block(tape, CAS_BLOCK_DATA, slice(payload, start, length));
    }
    add_cas_block(tape, CAS_BLOCK_EOF, {});
    return tape;
}

uint32_t granule_lsn(uint8_t granule)
{
    uint32_t track = granule / 2 + (granule >= 34 ? 1 : 0); // track 17 is the directory
    return track * DECB_SECTORS_TRACK + (granule % 2) * DECB_GRAN_SECTORS;
}

} // namespace

TEST_CASE("a .dsk mounts as DSK, writable or read-only")
{
    for (auto mode : {DISK_ACCESS_MODE_READ, DISK_ACCESS_MODE_WRITE})
    {
        MemFile file(numbered_sectors(4));
        drivewireDisk disk;
        CHECK(disk.mount(&file, DSK_TEST_FILE, file.contents().size(), mode).is_success());
        CHECK(disk.device_active);
        CHECK(disk.mediatype() == MEDIATYPE_DSK);
        CHECK(disk.get_media_status() == DISK_CTRL_STATUS_CLEAR);
        check_closed_once(disk, file);
    }
}

TEST_CASE("every DSK sector reads back as stored, in any order")
{
    auto image = numbered_sectors(4);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
           .is_success());

    for (uint32_t lsn : {2, 0, 1, 3, 3, 1})
    {
        CAPTURE(lsn);
        CHECK(coco_read(disk, lsn) == sector_of(image, lsn));
        CHECK(dragon_read(disk, lsn) == sector_of(image, lsn));
    }
    check_closed_once(disk, file);
}

TEST_CASE("a DSK read at the block count reports EOF")
{
    MemFile file(numbered_sectors(4));
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, file.contents().size(), DISK_ACCESS_MODE_READ)
           .is_success());

    CHECK(disk.read(3, nullptr).is_success());
    CHECK(disk.read(4, nullptr).is_error());
    CHECK(disk.get_media_status() == EOF_STATUS);
    check_closed_once(disk, file);
}

TEST_CASE("a DSK write lands in its sector and reads back")
{
    auto image = numbered_sectors(4);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
           .is_success());

    REQUIRE(coco_read(disk, 2) == sector_of(image, 2));
    CHECK(write_filled(disk, 2, 0x77).is_success());

    std::vector<uint8_t> expected = image;
    std::fill_n(expected.begin() + 2 * MEDIA_BLOCK_SIZE, MEDIA_BLOCK_SIZE, 0x77);
    CHECK(file.contents() == expected);
    CHECK(coco_read(disk, 2) == sector_of(expected, 2));
    check_closed_once(disk, file);
}

TEST_CASE("a DSK write past the end, or to a read-only mount, is refused")
{
    for (auto mode : {DISK_ACCESS_MODE_READ, DISK_ACCESS_MODE_WRITE})
    {
        auto image = numbered_sectors(4);
        MemFile file(image);
        drivewireDisk disk;
        REQUIRE(disk.mount(&file, DSK_TEST_FILE, image.size(), mode).is_success());

        if (mode == DISK_ACCESS_MODE_READ)
            CHECK(write_filled(disk, 1, 0x77).is_error());
        CHECK(write_filled(disk, 4, 0x77).is_error());
        CHECK(disk.get_media_status() == EOF_STATUS);
        CHECK(file.contents() == image);
        check_closed_once(disk, file);
    }
}

TEST_CASE("a high-score sector on a read-only DSK is written through the host")
{
    constexpr uint32_t HIGH_SCORE_LSN = 100;
    auto image = numbered_sectors(330);
    const uint8_t marker[] = {'F', 'N', 'H', 'S', 1, 1, 0, HIGH_SCORE_LSN, 0, 2};
    memcpy(image.data() + 323 * MEDIA_BLOCK_SIZE, marker, sizeof(marker));

    host_files.reset();
    host_files.files[DSK_TEST_FILE] = image;
    auto file = MemFile::sharing(host_files.files[DSK_TEST_FILE]);
    fujiHost host;

    drivewireDisk disk;
    REQUIRE(disk.mount(file.get(), DSK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
           .is_success());
    disk.set_media_host(&host);

    CHECK(write_filled(disk, HIGH_SCORE_LSN + 1, 0x77).is_success());
    CHECK(write_filled(disk, HIGH_SCORE_LSN + 2, 0x77).is_error());
    CHECK(sector_of(file->contents(), HIGH_SCORE_LSN + 1)
          == std::vector<uint8_t>(MEDIA_BLOCK_SIZE, 0x77));
    CHECK(sector_of(file->contents(), HIGH_SCORE_LSN + 2)
          == sector_of(image, HIGH_SCORE_LSN + 2));
    CHECK(coco_read(disk, HIGH_SCORE_LSN + 1) == std::vector<uint8_t>(MEDIA_BLOCK_SIZE, 0x77));

    REQUIRE(host_files.opened.size() == 1);
    CHECK(host_files.opened[0]->closes == 1);
    check_closed_once(disk, *file);
}

TEST_CASE("unmount closes the image once and leaves nothing to read")
{
    MemFile file(numbered_sectors(4));
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, DSK_TEST_FILE, file.contents().size(), DISK_ACCESS_MODE_READ)
           .is_success());

    check_closed_once(disk, file);
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(disk.get_media_status() == NO_MEDIA_STATUS);
    CHECK(disk.read(0, nullptr).is_error());
    CHECK(write_filled(disk, 0, 0x77).is_error());
    CHECK(file.closes == 1);
}

TEST_CASE("mounting over a disk closes the old image once")
{
    auto first_image = numbered_sectors(4);
    auto second_image = numbered_sectors(8);
    MemFile first(first_image);
    MemFile second(second_image);
    drivewireDisk disk;

    REQUIRE(disk.mount(&first, DSK_TEST_FILE, first_image.size(), DISK_ACCESS_MODE_READ)
           .is_success());
    REQUIRE(disk.mount(&second, DSK_TEST_FILE, second_image.size(), DISK_ACCESS_MODE_READ)
           .is_success());
    CHECK(first.closes == 1);
    CHECK(second.closes == 0);
    CHECK(coco_read(disk, 6) == sector_of(second_image, 6));
    check_closed_once(disk, second);
}

TEST_CASE("VDK sectors start at LSN 1, after the header")
{
    auto image = vdk_sectors(4);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, VDK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
           .is_success());
    CHECK(disk.device_active);

    for (uint32_t lsn : {3, 1, 2, 4, 4})
    {
        CAPTURE(lsn);
        CHECK(coco_read(disk, lsn) == sector_of(image, lsn - 1, VDK_HEADER_SIZE));
        CHECK(dragon_read(disk, lsn) == sector_of(image, lsn - 1, VDK_HEADER_SIZE));
    }
    CHECK(disk.read(5, nullptr).is_error());
    CHECK(disk.get_media_status() == EOF_STATUS);
    check_closed_once(disk, file);
}

TEST_CASE("a VDK write lands in its sector, and one past the end is refused")
{
    auto image = vdk_sectors(4);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, VDK_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
           .is_success());

    CHECK(write_filled(disk, 2, 0x77).is_success());
    CHECK(write_filled(disk, 5, 0x77).is_error());

    std::vector<uint8_t> expected = image;
    std::fill_n(expected.begin() + VDK_HEADER_SIZE + MEDIA_BLOCK_SIZE, MEDIA_BLOCK_SIZE, 0x77);
    CHECK(file.contents() == expected);
    CHECK(coco_read(disk, 2) == std::vector<uint8_t>(MEDIA_BLOCK_SIZE, 0x77));
    check_closed_once(disk, file);
}

TEST_CASE("a VDK write to a read-only mount is refused")
{
    auto image = vdk_sectors(4);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, VDK_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
           .is_success());

    CHECK(write_filled(disk, 2, 0x77).is_error());
    CHECK(disk.get_media_status() == EOF_STATUS);
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("an MRM reads in 32K blocks, padding the last with $FF")
{
    auto image = make_image(MRM_BLOCK + 300);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, MRM_TEST_FILE, image.size(), DISK_ACCESS_MODE_READ)
           .is_success());

    CHECK(coco_read(disk, 0) == slice(image, 0, MRM_BLOCK));

    std::vector<uint8_t> last(MRM_BLOCK, 0xFF);
    std::copy(image.begin() + MRM_BLOCK, image.end(), last.begin());
    CHECK(coco_read(disk, 1) == last);

    CHECK(disk.read(2, nullptr).is_error());
    CHECK(disk.get_media_status() == EOF_STATUS);
    check_closed_once(disk, file);
}

TEST_CASE("an MRM refuses writes")
{
    auto image = make_image(MRM_BLOCK);
    MemFile file(image);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, MRM_TEST_FILE, image.size(), DISK_ACCESS_MODE_WRITE)
           .is_success());

    CHECK(write_filled(disk, 0, 0x77).is_error());
    CHECK(file.contents() == image);
    check_closed_once(disk, file);
}

TEST_CASE("a .cas mounts as a Disk BASIC disk holding the tape's file")
{
    auto payload = make_image(300);
    auto tape = cas_with_file("HELLO", payload);
    MemFile file(tape);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, CAS_TEST_FILE, tape.size(), DISK_ACCESS_MODE_READ)
           .is_success());
    CHECK(disk.device_active);

    auto directory = coco_read(disk, DECB_LSN_DIR_FIRST);
    REQUIRE(directory.has_value());
    CHECK(std::string(directory->begin(), directory->begin() + 11) == "HELLO   DAT");
    CHECK((*directory)[11] == 1);    // data
    CHECK((*directory)[12] == 0xFF); // ASCII
    CHECK((*directory)[DECB_DIRENT_SIZE] == 0xFF); // no second entry

    uint32_t first_lsn = granule_lsn((*directory)[13]);
    CHECK(coco_read(disk, first_lsn) == slice(payload, 0, MEDIA_BLOCK_SIZE));
    auto second = coco_read(disk, first_lsn + 1);
    REQUIRE(second.has_value());
    CHECK(slice(*second, 0, 300 - MEDIA_BLOCK_SIZE)
          == slice(payload, MEDIA_BLOCK_SIZE, 300 - MEDIA_BLOCK_SIZE));

    auto name = coco_read(disk, DECB_LSN_DISKNAME);
    REQUIRE(name.has_value());
    CHECK(std::string((const char *)name->data()) == "tape");

    CHECK(disk.read(DECB_TOTAL_SECTORS, nullptr).is_error());
    CHECK(disk.get_media_status() == EOF_STATUS);
    check_closed_once(disk, file);
}

TEST_CASE("a .cas refuses writes, even on a writable mount")
{
    auto tape = cas_with_file("HELLO", make_image(300));
    MemFile file(tape);
    drivewireDisk disk;
    REQUIRE(disk.mount(&file, CAS_TEST_FILE, tape.size(), DISK_ACCESS_MODE_WRITE)
           .is_success());

    CHECK(write_filled(disk, DECB_LSN_DIR_FIRST, 0x77).is_error());
    CHECK(write_filled(disk, 0, 0x77).is_error());
    CHECK(file.contents() == tape);
    check_closed_once(disk, file);
}

TEST_CASE("a .cas holding no file doesn't mount, and its image is closed")
{
    std::vector<uint8_t> tape(512, CAS_LEADER_BYTE);
    MemFile file(tape);
    drivewireDisk disk;
    CHECK(disk.mount(&file, CAS_TEST_FILE, tape.size(), DISK_ACCESS_MODE_READ)
         .is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    disk.unmount();
    CHECK(file.closes == 1);
}

TEST_CASE("an image with an unknown extension doesn't mount, and its image is closed")
{
    std::vector<uint8_t> image(512, 0);
    MemFile file(image);
    drivewireDisk disk;

    CHECK(disk.mount(&file, "drivewire_disk_test.xyz", image.size(), DISK_ACCESS_MODE_READ)
         .is_error());
    CHECK_FALSE(disk.device_active);
    CHECK(disk.mediatype() == MEDIATYPE_UNKNOWN);
    CHECK(file.closes == 1);
    CHECK_FALSE(file.usedAfterClose);
    disk.unmount();
    CHECK(file.closes == 1);
}
