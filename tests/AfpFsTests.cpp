#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "AfpFakeBridge.h"

#include "fnFileAFP.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

// A session on volume Alpha of a freshly reset fake, to seed it and look inside.
struct Mounted
{
    AfpSession session;
    Mounted()
    {
        fake.reset();
        REQUIRE(session.open("host", 0, "user", "pw", "Alpha").is_success());
    }

    void add_file(const std::string &path, const std::string &data)
    {
        AfpFile f(session);
        REQUIRE(f.open(path, AfpOpenMode::Write).is_success());
        REQUIRE(f.write(data.data(), data.size()) == static_cast<int>(data.size()));
        REQUIRE(f.close().is_success());
    }

    bool has(const std::string &path)
    {
        AfpDirEntry e;
        return afp_stat(session, path, e) == 0;
    }
};

static std::string slurp(FileHandler &f)
{
    std::string out;
    char buf[7];
    size_t n;
    while ((n = f.read(buf, 1, sizeof(buf))) > 0)
        out.append(buf, n);
    return out;
}

TEST_CASE("handler: w creates, r reads back, and counts are in items")
{
    Mounted m;
    {
        FileHandlerAFP f(m.session);
        REQUIRE(f.open("/a.txt", "w").is_success());
        CHECK(f.write("abcdefgh", 3, 2) == 2); // six bytes
        CHECK(f.tell() == 6);
        CHECK(f.close(false) == 0);
    }

    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/a.txt", "r").is_success());
    char buf[16] = {0};
    CHECK(f.read(buf, 4, 2) == 1); // six bytes hold one whole item of four
    CHECK(std::string(buf, 6) == "abcdef");
    CHECK(f.read(buf, 4, 2) == 0);
    CHECK(f.close(false) == 0);
}

TEST_CASE("handler: a read is finished even when the server answers in pieces")
{
    Mounted m;
    m.add_file("/s.txt", "0123456789");
    fake.max_read = 3;
    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/s.txt", "r").is_success());
    char buf[16] = {0};
    CHECK(f.read(buf, 1, 10) == 10);
    CHECK(std::string(buf, 10) == "0123456789");
    f.close(false);
}

TEST_CASE("handler: r on a missing file fails; r+ needs the file too")
{
    Mounted m;
    FileHandlerAFP f(m.session);
    CHECK(f.open("/nope.txt", "r").is_error());
    CHECK(f.open("/nope.txt", "r+").is_error());
    CHECK(f.open("/nope.txt", "rb+").is_error());
    CHECK_FALSE(m.has("/nope.txt"));
}

TEST_CASE("handler: r+ reads and writes in place")
{
    Mounted m;
    m.add_file("/disk.img", "0123456789");
    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/disk.img", "rb+").is_success());
    CHECK(f.seek(4, SEEK_SET) == 0);
    CHECK(f.write("XY", 1, 2) == 2);
    CHECK(f.seek(0, SEEK_SET) == 0);
    CHECK(slurp(f) == "0123XY6789");
    f.close(false);
}

TEST_CASE("handler: w+ truncates and can be read; a+ keeps and appends")
{
    Mounted m;
    m.add_file("/log.txt", "old contents");
    {
        FileHandlerAFP f(m.session);
        REQUIRE(f.open("/log.txt", "w+").is_success());
        CHECK(f.write("new", 1, 3) == 3);
        CHECK(f.seek(0, SEEK_SET) == 0);
        CHECK(slurp(f) == "new");
        f.close(false);
    }
    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/log.txt", "a+").is_success());
    CHECK(f.tell() == 3);
    CHECK(f.write("er", 1, 2) == 2);
    CHECK(f.seek(0, SEEK_SET) == 0);
    CHECK(slurp(f) == "newer");
    f.close(false);
}

TEST_CASE("handler: seek from the current position and the end, and not before the start")
{
    Mounted m;
    m.add_file("/s.txt", "0123456789");
    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/s.txt", "r").is_success());
    CHECK(f.seek(-3, SEEK_END) == 0);
    CHECK(f.tell() == 7);
    CHECK(f.seek(-2, SEEK_CUR) == 0);
    CHECK(f.tell() == 5);
    CHECK(f.seek(-1, SEEK_SET) == -1);
    CHECK(f.seek(-20, SEEK_CUR) == -1);
    CHECK(f.tell() == 5);
    CHECK(f.seek(0, 99) == -1);
    f.close(false);
}

TEST_CASE("handler: after the session closes, calls fail and nothing crashes")
{
    Mounted m;
    m.add_file("/s.txt", "0123456789");
    FileHandlerAFP f(m.session);
    REQUIRE(f.open("/s.txt", "r+").is_success());
    m.session.close();

    char buf[4];
    CHECK(f.read(buf, 1, 4) == 0);
    CHECK(f.write("ab", 1, 2) == 0);
    f.close(false);
}

TEST_CASE("handler: a bad mode is refused")
{
    Mounted m;
    FileHandlerAFP f(m.session);
    CHECK(f.open("/a.txt", "").is_error());
    CHECK(f.open("/a.txt", "x").is_error());
    CHECK(f.open("/a.txt", nullptr).is_error());
    CHECK_FALSE(m.has("/a.txt"));
}
