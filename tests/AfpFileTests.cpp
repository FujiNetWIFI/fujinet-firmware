#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "AfpFakeBridge.h"
#include "afpFile.h"
#include "afpSession.h"

#include <cerrno>
#include <string>
#include <vector>

// A session on volume Alpha of a freshly reset fake.
struct Mounted
{
    AfpSession session;
    Mounted()
    {
        fake.reset();
        REQUIRE(session.open("server", 0, "alice", "secret", "Alpha").is_success());
    }
};

static std::string read_whole(AfpSession &session, const std::string &path)
{
    AfpFile f(session);
    REQUIRE(f.open(path, AfpOpenMode::Read).is_success());
    std::string out;
    char buf[7];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    REQUIRE(n == 0);
    return out;
}

static void write_whole(AfpSession &session, const std::string &path, const std::string &data,
                        AfpOpenMode mode = AfpOpenMode::Write)
{
    AfpFile f(session);
    REQUIRE(f.open(path, mode).is_success());
    REQUIRE(f.write(data.data(), data.size()) == static_cast<int>(data.size()));
    REQUIRE(f.close().is_success());
}

TEST_CASE("what is written can be read back")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "hello, afp");
    CHECK(read_whole(m.session, "/note.txt") == "hello, afp");
}

TEST_CASE("opening for read reports the size and starts at the beginning")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "0123456789");
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Read).is_success());
    CHECK(f.is_open());
    CHECK(f.size() == 10);
    CHECK(f.tell() == 0);
}

TEST_CASE("a read that crosses the end returns what is there, then nothing")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "abcde");
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Read).is_success());
    char buf[16];
    CHECK(f.read(buf, sizeof(buf)) == 5);
    CHECK(f.tell() == 5);
    CHECK(f.read(buf, sizeof(buf)) == 0);
    CHECK(f.read(buf, sizeof(buf)) == 0);
}

TEST_CASE("seek moves the next read")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "0123456789");
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Read).is_success());
    f.seek(7);
    char buf[4] = {0};
    CHECK(f.read(buf, 3) == 3);
    CHECK(std::string(buf, 3) == "789");
}

TEST_CASE("opening for write truncates what was there")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "a long first version");
    write_whole(m.session, "/note.txt", "short");
    CHECK(read_whole(m.session, "/note.txt") == "short");
}

TEST_CASE("opening for append continues at the end, and creates a missing file")
{
    Mounted m;
    write_whole(m.session, "/log.txt", "one,", AfpOpenMode::Append);
    write_whole(m.session, "/log.txt", "two", AfpOpenMode::Append);
    CHECK(read_whole(m.session, "/log.txt") == "one,two");
}

TEST_CASE("update reads and writes in place without truncating")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "0123456789");
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Update).is_success());
    f.seek(2);
    CHECK(f.write("XY", 2) == 2);
    CHECK(f.size() == 10);
    REQUIRE(f.close().is_success());
    CHECK(read_whole(m.session, "/note.txt") == "01XY456789");
}

TEST_CASE("a write past the end grows the file")
{
    Mounted m;
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Write).is_success());
    CHECK(f.write("abc", 3) == 3);
    CHECK(f.write("def", 3) == 3);
    CHECK(f.size() == 6);
    CHECK(f.tell() == 6);
}

TEST_CASE("a missing file cannot be read or updated")
{
    Mounted m;
    AfpFile f(m.session);
    CHECK(f.open("/nope.txt", AfpOpenMode::Read).is_error());
    CHECK(f.error() == -ENOENT);
    CHECK_FALSE(f.is_open());
    CHECK(f.open("/nope.txt", AfpOpenMode::Update).is_error());
    CHECK(f.error() == -ENOENT);
}

TEST_CASE("a directory cannot be opened as a file")
{
    Mounted m;
    REQUIRE(afp_mkdir(m.session, "/docs") == 0);
    AfpFile f(m.session);
    CHECK(f.open("/docs", AfpOpenMode::Read).is_error());
    CHECK(f.error() == -EISDIR);
}

TEST_CASE("a directory is refused in every mode, even by a server that would open it")
{
    Mounted m;
    REQUIRE(afp_mkdir(m.session, "/docs") == 0);
    fake.dirs_open_as_files = true;
    for (AfpOpenMode mode : {AfpOpenMode::Read, AfpOpenMode::Write, AfpOpenMode::Append, AfpOpenMode::Update})
    {
        CAPTURE(static_cast<int>(mode));
        AfpFile f(m.session);
        CHECK(f.open("/docs", mode).is_error());
        CHECK(f.error() == -EISDIR);
        CHECK_FALSE(f.is_open());
        CHECK(fake.closes == fake.opens);
    }
}

TEST_CASE("an append whose size cannot be read fails instead of starting at the beginning")
{
    Mounted m;
    write_whole(m.session, "/log.txt", "keep this");
    fake.stat_error = -EIO;
    AfpFile f(m.session);
    CHECK(f.open("/log.txt", AfpOpenMode::Append).is_error());
    CHECK(f.error() == -EIO);
    CHECK_FALSE(f.is_open());
    CHECK(fake.closes == fake.opens);
    fake.stat_error = 0;
    CHECK(read_whole(m.session, "/log.txt") == "keep this");
}

TEST_CASE("a short read is not the end of the file")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "0123456789");
    fake.max_read = 3;
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Read).is_success());
    char buf[16];
    CHECK(f.read(buf, sizeof(buf)) == 3);
    CHECK(f.tell() == 3);
    CHECK(read_whole(m.session, "/note.txt") == "0123456789");
}

TEST_CASE("zero-length reads and writes do nothing")
{
    Mounted m;
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Write).is_success());
    char c = 0;
    CHECK(f.write(&c, 0) == 0);
    CHECK(f.read(&c, 0) == 0);
    CHECK(f.tell() == 0);
    CHECK(f.size() == 0);
}

TEST_CASE("a file cannot be created in a directory that does not exist")
{
    Mounted m;
    AfpFile f(m.session);
    CHECK(f.open("/nowhere/note.txt", AfpOpenMode::Write).is_error());
    CHECK(f.error() == -ENOENT);
}

TEST_CASE("reads and writes on a file that is not open are refused")
{
    Mounted m;
    AfpFile f(m.session);
    char buf[4];
    CHECK(f.read(buf, sizeof(buf)) == -EBADF);
    CHECK(f.write("x", 1) == -EBADF);
    CHECK(f.close().is_success());
}

TEST_CASE("the destructor closes the file")
{
    Mounted m;
    {
        AfpFile f(m.session);
        REQUIRE(f.open("/note.txt", AfpOpenMode::Write).is_success());
    }
    CHECK(fake.opens == 1);
    CHECK(fake.closes == 1);
}

TEST_CASE("opening again closes what was open")
{
    Mounted m;
    AfpFile f(m.session);
    REQUIRE(f.open("/a.txt", AfpOpenMode::Write).is_success());
    REQUIRE(f.open("/b.txt", AfpOpenMode::Write).is_success());
    CHECK(fake.closes == 1);
}

TEST_CASE("a session with no volume refuses every operation")
{
    fake.reset();
    AfpSession session;
    AfpFile f(session);
    AfpDirEntry entry;
    std::vector<AfpDirEntry> entries;
    CHECK(f.open("/note.txt", AfpOpenMode::Write).is_error());
    CHECK(f.error() == -ENOTCONN);
    CHECK(afp_stat(session, "/", entry) == -ENOTCONN);
    CHECK(afp_list(session, "/", entries) == -ENOTCONN);
    CHECK(afp_remove(session, "/x") == -ENOTCONN);
    CHECK(afp_rename(session, "/x", "/y") == -ENOTCONN);
    CHECK(afp_mkdir(session, "/x") == -ENOTCONN);
    CHECK(afp_rmdir(session, "/x") == -ENOTCONN);
}

TEST_CASE("after the server ends the session an open file fails cleanly and is not closed twice")
{
    Mounted m;
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Write).is_success());

    // What the library does: unmount, which frees every open file handle.
    afp_volume *v = m.session.volume();
    v->attached = false;
    fake.gone(v);

    char buf[4];
    CHECK(f.read(buf, sizeof(buf)) == -ENOTCONN);
    CHECK(f.write("x", 1) == -ENOTCONN);
    CHECK(f.close().is_success());
    CHECK_FALSE(f.is_open());
    CHECK(fake.closes == 0);
}

TEST_CASE("a file opened before the session was reopened is not handed back to the library")
{
    Mounted m;
    AfpFile f(m.session);
    REQUIRE(f.open("/note.txt", AfpOpenMode::Write).is_success());

    // Closing the session unmounts, which frees the handle; reopening
    // attaches the same volume again.
    m.session.close();
    REQUIRE(m.session.open("server", 0, "alice", "secret", "Alpha").is_success());

    char buf[4];
    CHECK(f.read(buf, sizeof(buf)) == -ENOTCONN);
    CHECK(f.write("x", 1) == -ENOTCONN);
    CHECK(f.close().is_success());
    CHECK(fake.closes == 0);
}

TEST_CASE("stat tells files from directories")
{
    Mounted m;
    write_whole(m.session, "/note.txt", "12345");
    REQUIRE(afp_mkdir(m.session, "/docs") == 0);

    AfpDirEntry e;
    REQUIRE(afp_stat(m.session, "/note.txt", e) == 0);
    CHECK(e.name == "note.txt");
    CHECK_FALSE(e.is_dir);
    CHECK(e.size == 5);

    REQUIRE(afp_stat(m.session, "/docs", e) == 0);
    CHECK(e.is_dir);

    REQUIRE(afp_stat(m.session, "/", e) == 0);
    CHECK(e.is_dir);
    CHECK(e.name.empty());

    CHECK(afp_stat(m.session, "/nope", e) == -ENOENT);
}

TEST_CASE("a listing has every entry of that directory and no others")
{
    Mounted m;
    write_whole(m.session, "/a.txt", "1");
    REQUIRE(afp_mkdir(m.session, "/docs") == 0);
    write_whole(m.session, "/docs/inner.txt", "22");

    std::vector<AfpDirEntry> root;
    REQUIRE(afp_list(m.session, "/", root) == 0);
    REQUIRE(root.size() == 2);
    CHECK(root[0].name == "a.txt");
    CHECK_FALSE(root[0].is_dir);
    CHECK(root[0].size == 1);
    CHECK(root[1].name == "docs");
    CHECK(root[1].is_dir);

    std::vector<AfpDirEntry> docs;
    REQUIRE(afp_list(m.session, "/docs", docs) == 0);
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "inner.txt");
    CHECK(docs[0].size == 2);
}

TEST_CASE("listing replaces what the vector held")
{
    Mounted m;
    std::vector<AfpDirEntry> entries(3);
    REQUIRE(afp_list(m.session, "/", entries) == 0);
    CHECK(entries.empty());
}

TEST_CASE("listing something that is not a directory is an error, not an empty listing")
{
    Mounted m;
    write_whole(m.session, "/a.txt", "1");
    std::vector<AfpDirEntry> entries;
    CHECK(afp_list(m.session, "/nope", entries) == -ENOENT);
    CHECK(afp_list(m.session, "/a.txt", entries) == -ENOTDIR);
}

TEST_CASE("remove, rename, mkdir and rmdir report what the server says")
{
    Mounted m;
    write_whole(m.session, "/a.txt", "1");

    CHECK(afp_rename(m.session, "/a.txt", "/b.txt") == 0);
    CHECK(read_whole(m.session, "/b.txt") == "1");
    CHECK(afp_rename(m.session, "/a.txt", "/c.txt") == -ENOENT);

    CHECK(afp_mkdir(m.session, "/docs") == 0);
    CHECK(afp_mkdir(m.session, "/docs") == -EEXIST);
    write_whole(m.session, "/docs/inner.txt", "2");
    CHECK(afp_rmdir(m.session, "/docs") == -ENOTEMPTY);
    CHECK(afp_remove(m.session, "/docs/inner.txt") == 0);
    CHECK(afp_rmdir(m.session, "/docs") == 0);

    CHECK(afp_remove(m.session, "/b.txt") == 0);
    CHECK(afp_remove(m.session, "/b.txt") == -ENOENT);
}
