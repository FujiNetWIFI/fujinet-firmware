#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// The CPM: adapter's header chain up to disk.h, as lib/network-protocol/CPM.cpp includes it.
// cpu.h and cpm.h stay out: on some targets the BDOS prints through SYSTEM_BUS.
#define RUNCPM_STATIC_IMPL
#include "abstraction_network_protocol.h"
#include "globals.h"
#include "ram.h"
#include "console.h"
// The CPU state disk.h touches, from cpu.h
static int32 DE;
static int32 Status;
#include "disk.h"

// DirCache only calls this with a pattern, and these tests pass none.
bool util_wildcard_match(const char *, const char *) { return true; }

namespace
{

namespace fsys = std::filesystem;

// Relative to the ctest working directory, as in FsSDListingTests.cpp
#define SD_ROOT "runcpm_disk_test.tmp"
const fsys::path drive_dir = SD_ROOT "/CPM/A/0";

constexpr uint16_t fcb_addr = 0x1000;
constexpr uint16_t dma_addr = 0x0080;

struct CpmDisk
{
    CpmDisk()
    {
        static bool started = false;
        _sys_closeall();
        fsys::remove_all(SD_ROOT);
        fsys::create_directories(drive_dir);
        if (!started)
        {
            REQUIRE(fnSDFAT.start(SD_ROOT).is_success());
            started = true;
        }
        if (RAM == nullptr)
            RAM = static_cast<uint8_t *>(calloc(MEMSIZE, 1));
        REQUIRE(RAM != nullptr);
        memset(RAM, 0, MEMSIZE);
        cDrive = 0;
        userCode = 0;
        roVector = 0;
        loginVector = 0;
        dmaAddr = dma_addr;
        // _error() waits for a key; this makes it return at once
        _cpm_stop_requested = true;
    }

    ~CpmDisk()
    {
        _sys_closeall();
        // Windows won't delete a read-only file
        std::error_code ignored;
        for (const auto &entry : fsys::directory_iterator(drive_dir, ignored))
            fsys::permissions(entry.path(), fsys::perms::owner_write, fsys::perm_options::add, ignored);
    }
};

void set_name(uint16_t addr, const std::string &name, const std::string &ext)
{
    memset(&RAM[addr], ' ', 12);
    RAM[addr] = 0;
    memcpy(&RAM[addr + 1], name.data(), name.size());
    memcpy(&RAM[addr + 9], ext.data(), ext.size());
}

void set_fcb(const std::string &name, const std::string &ext)
{
    memset(&RAM[fcb_addr], 0, 36);
    set_name(fcb_addr, name, ext);
}

void set_record(uint32_t record)
{
    RAM[fcb_addr + 33] = record & 0xFF;
    RAM[fcb_addr + 34] = (record >> 8) & 0xFF;
    RAM[fcb_addr + 35] = (record >> 16) & 0xFF;
}

void fill_dma(uint8_t value)
{
    memset(&RAM[dma_addr], value, BlkSZ);
}

bool dma_is(uint8_t value)
{
    for (int idx = 0; idx < BlkSZ; ++idx)
    {
        if (RAM[dma_addr + idx] != value)
            return false;
    }
    return true;
}

std::vector<uint8_t> host_bytes(const std::string &name)
{
    std::ifstream input(drive_dir / name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
}

void write_host(const std::string &name, const std::vector<uint8_t> &bytes)
{
    std::ofstream out(drive_dir / name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

#ifndef _WIN32
size_t open_fds()
{
    size_t count = 0;
    for ([[maybe_unused]] const auto &entry : fsys::directory_iterator("/dev/fd"))
        ++count;
    return count;
}
#endif

void write_seq_records(int count, uint8_t first)
{
    for (int idx = 0; idx < count; ++idx)
    {
        fill_dma(first + idx);
        REQUIRE(_WriteSeq(fcb_addr) == 0);
    }
}

} // namespace

TEST_CASE("BDOS 35 sees records written before the file is closed")
{
    CpmDisk disk;
    set_fcb("SIZE", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(3, 1);

    DE = fcb_addr;
    _GetFileSize(fcb_addr);
    CHECK(RAM[fcb_addr + 33] == 3);
    CHECK(RAM[fcb_addr + 34] == 0);
}

TEST_CASE("a short last record reads padded with ^Z")
{
    CpmDisk disk;
    std::vector<uint8_t> bytes(BlkSZ + 5, 'B');
    std::fill_n(bytes.begin(), BlkSZ, 'A');
    write_host("TAIL.TXT", bytes);

    set_fcb("TAIL", "TXT");
    REQUIRE(_OpenFile(fcb_addr) == 0);
    fill_dma(0);
    CHECK(_ReadSeq(fcb_addr) == 0);
    CHECK(dma_is('A'));

    fill_dma(0);
    CHECK(_ReadSeq(fcb_addr) == 0);
    for (int idx = 0; idx < BlkSZ; ++idx)
        CHECK(RAM[dma_addr + idx] == (idx < 5 ? 'B' : 0x1a));
    CHECK(_ReadSeq(fcb_addr) == 1);

    fill_dma(0);
    set_record(1);
    CHECK(_ReadRand(fcb_addr) == 0);
    CHECK(RAM[dma_addr] == 'B');
    CHECK(RAM[dma_addr + 5] == 0x1a);
}

TEST_CASE("a random read past the end returns 1 and leaves the file alone")
{
    CpmDisk disk;
    set_fcb("SHORT", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(2, 1);

    set_record(10);
    CHECK(_ReadRand(fcb_addr) == 1);
    _sys_closeall();
    CHECK(host_bytes("SHORT.DAT").size() == 2 * BlkSZ);
}

TEST_CASE("a random write past the end fills the gap with zeros")
{
    CpmDisk disk;
    set_fcb("GAP", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    fill_dma(0xA5);
    set_record(5);
    REQUIRE(_WriteRand(fcb_addr) == 0);
    _sys_closeall();

    std::vector<uint8_t> bytes = host_bytes("GAP.DAT");
    REQUIRE(bytes.size() == 6 * BlkSZ);
    CHECK(std::all_of(bytes.begin(), bytes.begin() + 5 * BlkSZ, [](uint8_t val) { return val == 0; }));
    CHECK(std::all_of(bytes.begin() + 5 * BlkSZ, bytes.end(), [](uint8_t val) { return val == 0xA5; }));
}

TEST_CASE("a record reads back from the file it was just written to")
{
    CpmDisk disk;
    set_fcb("BACK", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(2, 0x11);

    fill_dma(0);
    set_record(0);
    REQUIRE(_ReadRand(fcb_addr) == 0);
    CHECK(dma_is(0x11));
}

TEST_CASE("bytes that text mode would change come back as written")
{
    CpmDisk disk;
    std::vector<uint8_t> bytes(BlkSZ, 0x55);
    bytes[0] = 0x0D;
    bytes[1] = 0x0A;
    bytes[2] = 0x0A;
    bytes[3] = 0x1A;
    bytes[4] = 0x0D;
    set_fcb("BIN", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    memcpy(&RAM[dma_addr], bytes.data(), BlkSZ);
    REQUIRE(_WriteSeq(fcb_addr) == 0);
    _sys_closeall();
    CHECK(host_bytes("BIN.DAT") == bytes);

    set_fcb("BIN", "DAT");
    REQUIRE(_OpenFile(fcb_addr) == 0);
    fill_dma(0);
    REQUIRE(_ReadSeq(fcb_addr) == 0);
    CHECK(memcmp(&RAM[dma_addr], bytes.data(), BlkSZ) == 0);
}

TEST_CASE("a file made again after a delete starts empty")
{
    CpmDisk disk;
    set_fcb("AGAIN", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(3, 1);
    REQUIRE(_DeleteFile(fcb_addr) == 0);

    set_fcb("AGAIN", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(1, 0x77);
    REQUIRE(_CloseFile(fcb_addr) == 0);

    std::vector<uint8_t> bytes = host_bytes("AGAIN.DAT");
    REQUIRE(bytes.size() == BlkSZ);
    CHECK(bytes[0] == 0x77);
}

TEST_CASE("renaming an open file keeps its records")
{
    CpmDisk disk;
    set_fcb("OLD", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(2, 1);

    set_name(fcb_addr + 16, "NEW", "DAT");
    REQUIRE(_RenameFile(fcb_addr) == 0);
    _sys_closeall();

    CHECK_FALSE(fsys::exists(drive_dir / "OLD.DAT"));
    std::vector<uint8_t> bytes = host_bytes("NEW.DAT");
    REQUIRE(bytes.size() == 2 * BlkSZ);
    CHECK(bytes[BlkSZ] == 2);
}

TEST_CASE("closing gives the handles back with every record on disk")
{
    CpmDisk disk;
#ifndef _WIN32
    size_t baseline = open_fds();
#endif

    set_fcb("ONE", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(2, 1);
    set_fcb("TWO", "DAT");
    REQUIRE(_MakeFile(fcb_addr) == 0);
    write_seq_records(3, 1);
#ifndef _WIN32
    CHECK(open_fds() == baseline + 2);
#endif

    REQUIRE(_CloseFile(fcb_addr) == 0);
#ifndef _WIN32
    CHECK(open_fds() == baseline + 1);
#endif
    _sys_closeall();
#ifndef _WIN32
    CHECK(open_fds() == baseline);
#endif

    CHECK(host_bytes("ONE.DAT").size() == 2 * BlkSZ);
    CHECK(host_bytes("TWO.DAT").size() == 3 * BlkSZ);
}

#ifndef _WIN32
TEST_CASE("no more than four files stay open")
{
    CpmDisk disk;
    size_t baseline = open_fds();

    for (int idx = 0; idx < 6; ++idx)
    {
        std::string name = "FILE" + std::to_string(idx);
        write_host(name + ".DAT", std::vector<uint8_t>(BlkSZ, static_cast<uint8_t>(idx)));
        set_fcb(name, "DAT");
        REQUIRE(_OpenFile(fcb_addr) == 0);
        REQUIRE(_ReadSeq(fcb_addr) == 0);
        CHECK(dma_is(idx));
    }
    CHECK(open_fds() == baseline + CPM_OPEN_FILES);
}
#endif

TEST_CASE("a read-only file reads, sequentially and at random")
{
    CpmDisk disk;
    write_host("LOCKED.DAT", std::vector<uint8_t>(2 * BlkSZ, 0x5A));
    fsys::permissions(drive_dir / "LOCKED.DAT", fsys::perms::owner_read | fsys::perms::group_read
                                                  | fsys::perms::others_read);
    set_fcb("LOCKED", "DAT");
    REQUIRE(_OpenFile(fcb_addr) == 0);
    REQUIRE(_ReadSeq(fcb_addr) == 0);
    CHECK(dma_is(0x5A));
    set_record(1);
    CHECK(_ReadRand(fcb_addr) == 0);
}

TEST_CASE("a write to a file opened for reading lands in the file")
{
    CpmDisk disk;
    write_host("EDIT.DAT", std::vector<uint8_t>(2 * BlkSZ, 0x01));
    set_fcb("EDIT", "DAT");
    REQUIRE(_OpenFile(fcb_addr) == 0);
    REQUIRE(_ReadSeq(fcb_addr) == 0);

    fill_dma(0x02);
    set_record(1);
    REQUIRE(_WriteRand(fcb_addr) == 0);
    _sys_closeall();

    std::vector<uint8_t> bytes = host_bytes("EDIT.DAT");
    REQUIRE(bytes.size() == 2 * BlkSZ);
    CHECK(bytes[0] == 0x01);
    CHECK(bytes[BlkSZ] == 0x02);
}

TEST_CASE("a drive that was selected is not looked up again until it is logged out")
{
    CpmDisk disk;

    REQUIRE(_SelectDisk(1) == 0x00);
    fsys::remove_all(SD_ROOT "/CPM/A");
    CHECK(_SelectDisk(1) == 0x00);

    loginVector = 0;
    CHECK(_SelectDisk(1) == 0xFF);
}

TEST_CASE("a host file without an 8.3 name is left out of a directory search")
{
    CpmDisk disk;
    write_host("GOOD.TXT", {1});
    write_host("TOOLONGNAME.TXT", {1});
    write_host("A.LONGEXT", {1});
    write_host("A.B.C", {1});
    write_host(std::string(30, 'X') + ".TXT", {1});

    set_fcb("????????", "???");
    int hits = 0;
    for (uint8_t result = _SearchFirst(fcb_addr, TRUE); result != 0xFF;
         result = _SearchNext(fcb_addr, TRUE))
    {
        CHECK(std::string(reinterpret_cast<char *>(&RAM[dma_addr + 1]), 11) == "GOOD    TXT");
        ++hits;
    }
    CHECK(hits == 1);
}
