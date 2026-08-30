#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "IMDImage.h"

namespace
{

// The build forces FNIO_IS_STDIO for this target, so fnFile is std::FILE.
// Not tmpfile(): under MSYS2 that inherits msvcrt's create-in-the-drive-root
// behaviour, and Windows is in the PC CI matrix.
#define IMD_TEST_FILE "imdimage_test.tmp"

// Fixture builder following IMD.TXT section 6 field order.
struct IMD
{
    std::vector<uint8_t> out;

    IMD &header(const char *sig = "IMD 1.18: 01/01/2024 12:00:00\r\n",
                const char *comment = "test\r\n")
    {
        if (sig)
            for (const char *p = sig; *p; p++)
                out.push_back((uint8_t)*p);
        if (comment)
            for (const char *p = comment; *p; p++)
                out.push_back((uint8_t)*p);
        out.push_back(IMD_EOF_MARKER);
        return *this;
    }

    IMD &thdr(uint8_t mode, uint8_t cyl, uint8_t head_raw, uint8_t nsec, uint8_t size_code)
    {
        out.push_back(mode);
        out.push_back(cyl);
        out.push_back(head_raw);
        out.push_back(nsec);
        out.push_back(size_code);
        return *this;
    }

    IMD &map(const std::vector<uint8_t> &m)
    {
        for (uint8_t b : m)
            out.push_back(b);
        return *this;
    }

    IMD &sizes(const std::vector<uint16_t> &s)
    {
        for (uint16_t v : s)
        {
            out.push_back((uint8_t)(v & 0xFF));
            out.push_back((uint8_t)(v >> 8));
        }
        return *this;
    }

    IMD &unavailable()
    {
        out.push_back(0x00);
        return *this;
    }

    IMD &full(uint8_t type, uint16_t size, uint8_t fill)
    {
        out.push_back(type);
        for (uint16_t i = 0; i < size; i++)
            out.push_back(fill);
        return *this;
    }

    IMD &comp(uint8_t type, uint8_t fill)
    {
        out.push_back(type);
        out.push_back(fill);
        return *this;
    }

    IMD &raw(uint8_t b)
    {
        out.push_back(b);
        return *this;
    }

    IMD &pad(uint32_t n, uint8_t v)
    {
        for (uint32_t i = 0; i < n; i++)
            out.push_back(v);
        return *this;
    }

    // Sequential ids 1..nsec, size code 0 (128B), every sector compressed
    IMD &simple_track(uint8_t mode, uint8_t cyl, uint8_t head_raw, uint8_t nsec, uint8_t fill)
    {
        thdr(mode, cyl, head_raw, nsec, 0);
        std::vector<uint8_t> ids;
        for (uint8_t i = 1; i <= nsec; i++)
            ids.push_back(i);
        map(ids);
        for (uint8_t i = 0; i < nsec; i++)
            comp(0x02, fill);
        return *this;
    }
};

// Keeps file and image together; IMDImage does not own the file.
struct Mounted
{
    fnFile   *fp = nullptr;
    IMDImage  img;
    IMDStatus st = IMDStatus::IoError;

    Mounted(const IMD &b, bool writable = false)
    {
        fp = fopen(IMD_TEST_FILE, "w+b");
        REQUIRE(fp != nullptr);
        REQUIRE(fwrite(b.out.data(), 1, b.out.size(), fp) == b.out.size());
        rewind(fp);
        st = img.open(fp, (uint32_t)b.out.size(), writable);
    }

    ~Mounted()
    {
        img.close();
        fclose(fp);
        remove(IMD_TEST_FILE);
    }

    long file_size()
    {
        fflush(fp);
        fseek(fp, 0, SEEK_END);
        return ftell(fp);
    }
};

std::vector<uint8_t> read_lba(IMDImage &img, uint32_t lba, IMDStatus &st)
{
    std::vector<uint8_t> buf(img.sector_size(lba));
    uint16_t             got = 0;
    st = img.read_sector(lba, buf.data(), (uint32_t)buf.size(), &got);
    buf.resize(got);
    return buf;
}

bool all_equal(const std::vector<uint8_t> &v, uint8_t b)
{
    for (uint8_t x : v)
        if (x != b)
            return false;
    return !v.empty();
}

} // namespace

TEST_CASE("header and comment handling")
{
    SUBCASE("signature line is stripped, comment is kept")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(std::string(m.img.comment()) == "test\r\n");
    }

    SUBCASE("signature is optional - SIMH omits it entirely")
    {
        Mounted m(IMD().header(nullptr, "just a comment\r\n").simple_track(0, 0, 0, 2, 0x11));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lba_count() == 2);
        CHECK(std::string(m.img.comment()) == "just a comment\r\n");
    }

    SUBCASE("space-padded hour is accepted")
    {
        Mounted m(IMD().header("IMD 1.17: 17/01/2010  8:16:34\r\n", "cpm\r\n")
                      .simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(std::string(m.img.comment()) == "cpm\r\n");
    }

    SUBCASE("no 0x1A terminator is not an IMD")
    {
        IMD b;
        for (const char *p = "IMD 1.18: no terminator here"; *p; p++)
            b.out.push_back((uint8_t)*p);
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
        CHECK_FALSE(m.img.is_open());
    }

    SUBCASE("terminator with no tracks is not a usable image")
    {
        IMD b;
        b.out.push_back(IMD_EOF_MARKER);
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
    }
}

TEST_CASE("all nine sector record types")
{
    IMD b;
    b.header().thdr(0, 0, 0, 9, 0).map({1, 2, 3, 4, 5, 6, 7, 8, 9});
    b.unavailable();          // 0x00
    b.full(0x01, 128, 0x11);  // normal
    b.comp(0x02, 0x22);       // compressed
    b.full(0x03, 128, 0x33);  // deleted
    b.comp(0x04, 0x44);       // compressed deleted
    b.full(0x05, 128, 0x55);  // error
    b.comp(0x06, 0x66);       // compressed error
    b.full(0x07, 128, 0x77);  // deleted error
    b.comp(0x08, 0x88);       // compressed deleted error

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.lba_count() == 9);

    IMDSectorInfo si;

    SUBCASE("unavailable sectors are never fabricated")
    {
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.unavailable);
        IMDStatus st;
        read_lba(m.img, 0, st);
        CHECK(st == IMDStatus::Unavailable);
    }

    SUBCASE("flags decode from the (type-1) bit field")
    {
        struct { uint32_t lba; bool comp, del, err; } expect[] = {
            {1, false, false, false}, {2, true,  false, false},
            {3, false, true,  false}, {4, true,  true,  false},
            {5, false, false, true},  {6, true,  false, true},
            {7, false, true,  true},  {8, true,  true,  true},
        };
        for (auto &e : expect)
        {
            REQUIRE(m.img.sector_info(e.lba, si));
            CHECK(si.compressed == e.comp);
            CHECK(si.deleted == e.del);
            CHECK(si.had_error == e.err);
            CHECK_FALSE(si.unavailable);
        }
    }

    SUBCASE("compressed records expand to a full sector")
    {
        uint8_t fills[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        for (uint32_t i = 0; i < 8; i++)
        {
            IMDStatus st;
            auto d = read_lba(m.img, i + 1, st);
            CHECK(st == IMDStatus::Ok);
            CHECK(d.size() == 128);
            CHECK(all_equal(d, fills[i]));
        }
    }

    SUBCASE("data-error records still return their recovered contents")
    {
        IMDStatus st;
        auto d = read_lba(m.img, 5, st);
        CHECK(st == IMDStatus::Ok);
        REQUIRE(m.img.sector_info(5, si));
        CHECK(si.had_error);
        CHECK(all_equal(d, 0x55));
    }
}

TEST_CASE("malformed structures")
{
    SUBCASE("record type above 0x08 cannot be resynchronised")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);
        b.raw(0x09).pad(128, 0xBB); // bogus type, plus bytes so the tail is not padding
        Mounted m(b);
        CHECK(m.st == IMDStatus::BadRecordType);
    }

    SUBCASE("size code 7 is rejected")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 7).map({1, 2}).full(0x01, 128, 0xAA);
        Mounted m(b);
        CHECK(m.st == IMDStatus::BadSizeCode);
    }

    SUBCASE("a record cut short is truncated, not padding")
    {
        IMD b;
        b.header().thdr(0, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);
        b.raw(0x01).pad(40, 0xBB); // needs 128 payload bytes, only 40 present
        Mounted m(b);
        CHECK(m.st == IMDStatus::Truncated);
    }

    SUBCASE("a zero-sector track header is not valid")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 1, 0xE5);
        b.thdr(0, 1, 0, 0, 0).raw(0x77); // nsec 0, then a non-uniform tail
        Mounted m(b);
        CHECK(m.st == IMDStatus::NotIMD);
    }
}

TEST_CASE("trailing transport padding is tolerated")
{
    SUBCASE("XMODEM zero padding")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 26, 0xE5);
        b.pad(100, 0x00);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lba_count() == 26);
        CHECK(m.img.trailing_garbage() == 100);
    }

    SUBCASE("CP/M 0x1A padding")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 8, 0xE5);
        b.pad(37, 0x1A);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.lba_count() == 8);
        CHECK(m.img.trailing_garbage() == 37);
    }

    SUBCASE("a clean image reports no trailing bytes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 4, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.trailing_garbage() == 0);
    }
}

TEST_CASE("cylinder and head maps")
{
    SUBCASE("absent maps default to the track's physical cyl/head")
    {
        Mounted m(IMD().header().simple_track(5, 3, 1, 2, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.cyl == 3);
        CHECK(si.head == 1);

        IMDTrackInfo ti;
        REQUIRE(m.img.track_info(0, ti));
        CHECK_FALSE(ti.has_cyl_map);
        CHECK_FALSE(ti.has_head_map);
    }

    SUBCASE("cylinder map alone")
    {
        IMD b;
        b.header().thdr(5, 0, IMD_HEAD_CYL_MAP, 2, 0).map({1, 2}).map({77, 77});
        b.comp(0x02, 0xE5).comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.cyl == 77);
        CHECK(si.head == 0);
    }

    SUBCASE("head map alone")
    {
        IMD b;
        b.header().thdr(5, 2, IMD_HEAD_HEAD_MAP, 2, 0).map({1, 2}).map({1, 1});
        b.comp(0x02, 0xE5).comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.cyl == 2);
        CHECK(si.head == 1);
    }

    SUBCASE("both maps, cylinder first per spec")
    {
        IMD b;
        b.header()
            .thdr(5, 0, IMD_HEAD_CYL_MAP | IMD_HEAD_HEAD_MAP, 2, 2)
            .map({1, 2})
            .map({77, 77})  // cylinder map
            .map({1, 1});   // head map
        b.comp(0x02, 0xE5).unavailable();

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        REQUIRE(m.img.lba_count() == 2);

        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.cyl == 77);
        CHECK(si.head == 1);
        CHECK(si.size == 512);
        REQUIRE(m.img.sector_info(1, si));
        CHECK(si.unavailable);

        IMDTrackInfo ti;
        REQUIRE(m.img.track_info(0, ti));
        CHECK(ti.cyl == 0); // physical stays 0
        CHECK(ti.head == 0);
        CHECK(ti.has_cyl_map);
        CHECK(ti.has_head_map);
    }

    SUBCASE("head is masked to the low six bits")
    {
        // 0x41 = head map flag + head 1
        IMD b;
        b.header().thdr(5, 0, IMD_HEAD_HEAD_MAP | 0x01, 1, 0).map({1}).map({0});
        b.comp(0x02, 0xE5);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDTrackInfo ti;
        REQUIRE(m.img.track_info(0, ti));
        CHECK(ti.head == 1);
    }
}

TEST_CASE("sector sizes")
{
    SUBCASE("size codes 0..6 map to 128<<code")
    {
        for (uint8_t code = 0; code <= 6; code++)
        {
            IMD b;
            b.header().thdr(5, 0, 0, 1, code).map({1}).comp(0x02, 0xE5);
            Mounted m(b);
            REQUIRE(m.st == IMDStatus::Ok);
            CHECK(m.img.sector_size(0) == (uint16_t)(128 << code));
        }
    }

    SUBCASE("0xFF selects a per-sector little-endian size table")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, IMD_SIZE_CODE_VARIABLE).map({1, 2});
        b.sizes({128, 256});
        b.full(0x01, 128, 0xAA);
        b.comp(0x02, 0xBB);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.sector_size(0) == 128);
        CHECK(m.img.sector_size(1) == 256);
        CHECK(m.img.max_sector_size() == 256);
        CHECK_FALSE(m.img.uniform_sector_size());
        CHECK(m.img.linear_size() == 384);

        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 1, st), 0xBB));
        CHECK(st == IMDStatus::Ok);
    }

    SUBCASE("a uniform image reports uniform")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 26, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.uniform_sector_size());
        CHECK(m.img.max_sector_size() == 128);
    }

    SUBCASE("a buffer smaller than the sector is refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t small[64];
        CHECK(m.img.read_sector(0, small, sizeof(small), nullptr) == IMDStatus::BufferTooSmall);
    }

    SUBCASE("out_len may be null")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t buf[128];
        CHECK(m.img.read_sector(0, buf, sizeof(buf), nullptr) == IMDStatus::Ok);
    }
}

TEST_CASE("sector ordering")
{
    SUBCASE("interleaved ids are deinterleaved into LBA order")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 5, 0).map({1, 3, 5, 2, 4});
        // fill encodes the id so the mapping is checkable
        b.comp(0x02, 0x10).comp(0x02, 0x30).comp(0x02, 0x50);
        b.comp(0x02, 0x20).comp(0x02, 0x40);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        REQUIRE(m.img.lba_count() == 5);

        for (uint32_t i = 0; i < 5; i++)
        {
            IMDSectorInfo si;
            REQUIRE(m.img.sector_info(i, si));
            CHECK(si.id == i + 1);
            IMDStatus st;
            CHECK(all_equal(read_lba(m.img, i, st), (uint8_t)((i + 1) * 0x10)));
        }
    }

    SUBCASE("zero-based ids work without special casing")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({0, 1});
        b.comp(0x02, 0xA0).comp(0x02, 0xA1);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.id == 0);
        REQUIRE(m.img.sector_info(1, si));
        CHECK(si.id == 1);
    }

    SUBCASE("duplicate ids keep their on-disk order")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 3, 0).map({1, 1, 2});
        b.comp(0x02, 0xAA).comp(0x02, 0xBB).comp(0x02, 0xCC);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 0, st), 0xAA));
        CHECK(all_equal(read_lba(m.img, 1, st), 0xBB));
        CHECK(all_equal(read_lba(m.img, 2, st), 0xCC));
    }

    SUBCASE("find_lba locates by physical track and sector id")
    {
        IMD b;
        b.header().simple_track(0, 0, 0, 4, 0x11).simple_track(0, 1, 0, 4, 0x22);
        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);

        uint32_t lba = 0;
        REQUIRE(m.img.find_lba(1, 0, 3, lba));
        CHECK(lba == 6);
        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, lba, st), 0x22));
        CHECK_FALSE(m.img.find_lba(9, 0, 1, lba));
    }
}

TEST_CASE("mixed recording modes in one image")
{
    IMD b;
    b.header().simple_track(0, 0, 0, 26, 0xE5);  // FM track 0
    b.simple_track(5, 1, 0, 26, 0xE5);           // MFM elsewhere

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.track_count() == 2);

    IMDTrackInfo t0, t1;
    REQUIRE(m.img.track_info(0, t0));
    REQUIRE(m.img.track_info(1, t1));

    CHECK(t0.mode == 0);
    CHECK_FALSE(imd_mode_is_mfm(t0.mode));
    CHECK(imd_mode_rate_kbps(t0.mode) == 500);

    CHECK(t1.mode == 5);
    CHECK(imd_mode_is_mfm(t1.mode));
    CHECK(imd_mode_rate_kbps(t1.mode) == 250);
}

TEST_CASE("mode helpers")
{
    CHECK(imd_mode_rate_kbps(1) == 300);
    CHECK(imd_mode_rate_kbps(4) == 300);
    CHECK(imd_mode_is_mfm(3));
    CHECK_FALSE(imd_mode_is_mfm(2));
    CHECK(imd_mode_is_valid(5));
    CHECK_FALSE(imd_mode_is_valid(7));
    // libdsk 1Mbps extension: read-tolerated
    CHECK(imd_mode_is_valid(6));
    CHECK(imd_mode_is_valid(9));
    CHECK(imd_mode_is_mfm(9));
    CHECK_FALSE(imd_mode_is_mfm(6));
}

TEST_CASE("in-place writes")
{
    SUBCASE("a read-only mount refuses writes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), false);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.write_sector(0, d.data(), 128) == IMDStatus::ReadOnly);
    }

    SUBCASE("a full record takes any data and clears the error flag")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).full(0x07, 128, 0x00); // deleted + error
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128);
        for (size_t i = 0; i < d.size(); i++)
            d[i] = (uint8_t)i;
        REQUIRE(m.img.write_sector(0, d.data(), 128) == IMDStatus::Ok);

        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.rec_type == 0x03); // error cleared, deleted mark kept
        CHECK(si.deleted);
        CHECK_FALSE(si.had_error);

        IMDStatus st;
        CHECK(read_lba(m.img, 0, st) == d);
        CHECK(st == IMDStatus::Ok);
    }

    SUBCASE("a compressed record takes a uniform buffer")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).comp(0x08, 0xE5); // compressed+deleted+error
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128, 0x5A);
        REQUIRE(m.img.write_sector(0, d.data(), 128) == IMDStatus::Ok);

        IMDSectorInfo si;
        REQUIRE(m.img.sector_info(0, si));
        CHECK(si.rec_type == 0x04); // still compressed, still deleted, no error
        CHECK(si.compressed);
        CHECK(si.deleted);
        CHECK_FALSE(si.had_error);

        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 0, st), 0x5A));

        // The file must not have grown: the record is still one fill byte
        CHECK(m.file_size() == (long)b.out.size());
    }

    SUBCASE("a compressed record refuses a non-uniform buffer")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).comp(0x02, 0xE5);
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128, 0x5A);
        d[64] = 0x01;
        CHECK(m.img.write_sector(0, d.data(), 128) == IMDStatus::WriteRefused);

        IMDStatus st;
        CHECK(all_equal(read_lba(m.img, 0, st), 0xE5)); // untouched
    }

    SUBCASE("an unavailable record cannot absorb a write")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).unavailable();
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.write_sector(0, d.data(), 128) == IMDStatus::WriteRefused);
    }

    SUBCASE("the write length must match the sector exactly")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(64, 0x5A);
        CHECK(m.img.write_sector(0, d.data(), 64) == IMDStatus::WriteRefused);
    }

    SUBCASE("writing past the end is refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 1, 0xE5), true);
        REQUIRE(m.st == IMDStatus::Ok);
        std::vector<uint8_t> d(128, 0x5A);
        CHECK(m.img.write_sector(99, d.data(), 128) == IMDStatus::NoSuchSector);
    }
}

TEST_CASE("linear byte view")
{
    SUBCASE("reads span a sector boundary")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.comp(0x02, 0xAA).comp(0x02, 0xBB);

        Mounted m(b);
        REQUIRE(m.st == IMDStatus::Ok);
        CHECK(m.img.linear_size() == 256);

        uint8_t buf[64];
        REQUIRE(m.img.read_linear(96, buf, sizeof(buf)) == IMDStatus::Ok);
        for (int i = 0; i < 32; i++)
            CHECK(buf[i] == 0xAA);
        for (int i = 32; i < 64; i++)
            CHECK(buf[i] == 0xBB);
    }

    SUBCASE("reads past the end are refused")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 2, 0xE5));
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t buf[64];
        CHECK(m.img.read_linear(250, buf, sizeof(buf)) == IMDStatus::NoSuchSector);
    }

    SUBCASE("writes span a sector boundary")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA).full(0x01, 128, 0xBB);

        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(64, 0x5A);
        REQUIRE(m.img.write_linear(96, d.data(), (uint32_t)d.size()) == IMDStatus::Ok);

        std::vector<uint8_t> back(256);
        REQUIRE(m.img.read_linear(0, back.data(), 256) == IMDStatus::Ok);
        for (int i = 0; i < 96; i++)
            CHECK(back[i] == 0xAA);
        for (int i = 96; i < 160; i++)
            CHECK(back[i] == 0x5A);
        for (int i = 160; i < 256; i++)
            CHECK(back[i] == 0xBB);
    }

    SUBCASE("a refusal anywhere in the span leaves every sector untouched")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 2, 0).map({1, 2});
        b.full(0x01, 128, 0xAA);  // would accept anything
        b.comp(0x02, 0xBB);       // can only take a uniform buffer

        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        // Overwrites the tail of sector 0 and the head of sector 1; sector 1
        // would end up non-uniform, so the whole operation must be rejected.
        std::vector<uint8_t> d(64, 0x5A);
        CHECK(m.img.write_linear(96, d.data(), (uint32_t)d.size()) == IMDStatus::WriteRefused);

        std::vector<uint8_t> back(256);
        REQUIRE(m.img.read_linear(0, back.data(), 256) == IMDStatus::Ok);
        for (int i = 0; i < 128; i++)
            CHECK(back[i] == 0xAA);
        for (int i = 128; i < 256; i++)
            CHECK(back[i] == 0xBB);
    }

    SUBCASE("a uniform overwrite of a compressed sector is allowed")
    {
        IMD b;
        b.header().thdr(5, 0, 0, 1, 0).map({1}).comp(0x02, 0xBB);
        Mounted m(b, true);
        REQUIRE(m.st == IMDStatus::Ok);

        std::vector<uint8_t> d(128, 0xBB);
        CHECK(m.img.write_linear(0, d.data(), 128) == IMDStatus::Ok);
    }

    SUBCASE("read-only mounts refuse linear writes")
    {
        Mounted m(IMD().header().simple_track(0, 0, 0, 2, 0xE5), false);
        REQUIRE(m.st == IMDStatus::Ok);
        uint8_t d[16] = {0};
        CHECK(m.img.write_linear(0, d, sizeof(d)) == IMDStatus::ReadOnly);
    }
}

TEST_CASE("8 inch SSSD CP/M image - the most common IMD in existence")
{
    // 77 cylinders, 1 head, 26 sectors of 128 bytes, 500 kbps FM
    IMD b;
    b.header();
    for (uint8_t c = 0; c < 77; c++)
        b.simple_track(0, c, 0, 26, 0xE5);

    Mounted m(b);
    REQUIRE(m.st == IMDStatus::Ok);
    CHECK(m.img.track_count() == 77);
    CHECK(m.img.lba_count() == 2002);
    CHECK(m.img.linear_size() == 2002 * 128); // 256,256 bytes
    CHECK(m.img.uniform_sector_size());
    CHECK(m.img.trailing_garbage() == 0);

    IMDTrackInfo ti;
    REQUIRE(m.img.track_info(76, ti));
    CHECK(ti.cyl == 76);
    CHECK(ti.first_lba == 76 * 26);

    IMDStatus st;
    CHECK(all_equal(read_lba(m.img, 2001, st), 0xE5));
    CHECK(st == IMDStatus::Ok);
}

TEST_CASE("sector count is capped")
{
    // 258 tracks of 255 sectors overruns the uint16_t sector addressing that
    // the platform MediaType classes use.
    IMD b;
    b.header();
    for (int t = 0; t < 258; t++)
    {
        b.thdr(5, (uint8_t)(t & 0xFF), 0, 255, 0);
        std::vector<uint8_t> ids;
        for (int i = 0; i < 255; i++)
            ids.push_back((uint8_t)i);
        b.map(ids);
        for (int i = 0; i < 255; i++)
            b.comp(0x02, 0xE5);
    }

    Mounted m(b);
    CHECK(m.st == IMDStatus::TooLarge);
    CHECK_FALSE(m.img.is_open());
}

TEST_CASE("extension sniffing")
{
    CHECK(IMDImage::looks_like_imd_extension("disk.imd"));
    CHECK(IMDImage::looks_like_imd_extension("DISK.IMD"));
    CHECK(IMDImage::looks_like_imd_extension("/path/to/cpm.IMD"));
    CHECK_FALSE(IMDImage::looks_like_imd_extension("disk.img"));
    CHECK_FALSE(IMDImage::looks_like_imd_extension("imd"));
    CHECK_FALSE(IMDImage::looks_like_imd_extension(".imd"));
    CHECK_FALSE(IMDImage::looks_like_imd_extension(nullptr));
}

TEST_CASE("close resets the image")
{
    Mounted m(IMD().header().simple_track(0, 0, 0, 26, 0xE5));
    REQUIRE(m.st == IMDStatus::Ok);
    REQUIRE(m.img.lba_count() == 26);

    m.img.close();
    CHECK_FALSE(m.img.is_open());
    CHECK(m.img.lba_count() == 0);
    CHECK(m.img.track_count() == 0);
    CHECK(m.img.linear_size() == 0);

    uint8_t buf[128];
    CHECK(m.img.read_sector(0, buf, sizeof(buf), nullptr) == IMDStatus::IoError);
}

TEST_CASE("status strings are available for every code")
{
    CHECK(std::string(imd_status_str(IMDStatus::Ok)) == "Ok");
    CHECK(std::string(imd_status_str(IMDStatus::WriteRefused)) == "WriteRefused");
    CHECK(std::string(imd_status_str(IMDStatus::BadRecordType)) == "BadRecordType");
}
