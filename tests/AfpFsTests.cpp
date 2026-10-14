#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "AfpFakeBridge.h"

#include "fnFileAFP.h"
#include "fnFsAFP.h"

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

// utils.cpp refers to the speech synthesiser, which is not linked here.
int sam(int, char **) { return 0; }

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

static const char *mount_url = "afp://user:pw@host/Alpha";

TEST_CASE("fs: start needs a volume and a server that lets it in")
{
    fake.reset();
    FileSystemAFP fs;
    CHECK(fs.start(nullptr).is_error());
    CHECK(fs.start("").is_error());
    CHECK(fs.start("afp://host/").is_error());
    CHECK(fs.start("afp://user:pw@host/NoSuchVolume").is_error());
    CHECK_FALSE(fs.running());

    fake.login_error = -EACCES;
    CHECK(fs.start(mount_url).is_error());
    CHECK_FALSE(fs.running());

    // A failed start leaves nothing behind, so the next one can work.
    fake.login_error = 0;
    CHECK(fs.start(mount_url).is_success());
    CHECK(fs.running());
    CHECK(fs.start(mount_url).is_error());
}

TEST_CASE("fs: URL credentials win; the arguments are used when it has none")
{
    fake.reset();
    {
        FileSystemAFP fs;
        REQUIRE(fs.start("afp://alice:secret@host/Alpha", "bob", "other").is_success());
        CHECK(fake.last_user == "alice");
        CHECK(fake.last_password == "secret");
    }
    {
        FileSystemAFP fs;
        REQUIRE(fs.start("afp://host/Alpha", "bob", "other").is_success());
        CHECK(fake.last_user == "bob");
        CHECK(fake.last_password == "other");
    }
    FileSystemAFP fs;
    REQUIRE(fs.start("AFP://host/Alpha").is_success());
    CHECK(fake.last_user == "");
}

TEST_CASE("fs: nothing works before start")
{
    fake.reset();
    FileSystemAFP fs;
    CHECK_FALSE(fs.exists("/a.txt"));
    CHECK_FALSE(fs.is_dir("/"));
    CHECK(fs.mkdir("/sub").is_error());
    CHECK(fs.remove("/a.txt").is_error());
    CHECK(fs.dir_open("/", nullptr, 0).is_error());
    CHECK(fs.filehandler_open("/a.txt", "w") == nullptr);
}

TEST_CASE("fs: exists, is_dir, mkdir, rmdir, rename and remove")
{
    Mounted m;
    m.add_file("/a.txt", "abc");
    FileSystemAFP fs;
    REQUIRE(fs.start(mount_url).is_success());

    CHECK(fs.exists("/a.txt"));
    CHECK_FALSE(fs.exists("/b.txt"));
    CHECK_FALSE(fs.is_dir("/a.txt"));
    CHECK(fs.is_dir("/"));

    CHECK(fs.mkdir("/sub").is_success());
    CHECK(fs.is_dir("/sub"));
    CHECK(fs.dir_exists("/sub/"));
    CHECK(fs.mkdir("/sub").is_error());

    CHECK(fs.rename("/a.txt", "/sub/b.txt").is_success());
    CHECK(fs.exists("/sub/b.txt"));
    CHECK(fs.rmdir("/sub").is_error()); // not empty

    // remove() takes files and directories alike, as the other file systems do.
    CHECK(fs.remove("/sub/b.txt").is_success());
    CHECK(fs.remove("/sub").is_success());
    CHECK_FALSE(fs.exists("/sub"));
    CHECK(fs.remove("/sub").is_error());
    CHECK(fs.remove(nullptr).is_error());

    CHECK(fs.mkdir("/again").is_success());
    CHECK(fs.rmdir("/again").is_success());
    CHECK_FALSE(m.has("/again"));
}

static std::vector<std::string> names_of(FileSystemAFP &fs)
{
    std::vector<std::string> names;
    for (fsdir_entry *e = fs.dir_read(); e != nullptr; e = fs.dir_read())
        names.push_back(e->filename);
    return names;
}

static bool in(const std::vector<std::string> &names, const std::string &name)
{
    return std::count(names.begin(), names.end(), name) == 1;
}

TEST_CASE("fs: a listing skips dot files, filters, and is reused for the same directory")
{
    Mounted m;
    m.add_file("/b.atr", "22");
    m.add_file("/a.atr", "1");
    m.add_file("/c.txt", "333");
    m.add_file("/.hidden", "x");
    REQUIRE(afp_mkdir(m.session, "/games") == 0);
    FileSystemAFP fs;
    REQUIRE(fs.start(mount_url).is_success());

    REQUIRE(fs.dir_open("/", "*.atr", 0).is_success());
    std::vector<std::string> names = names_of(fs);
    CHECK(in(names, "a.atr"));
    CHECK(in(names, "b.atr"));
    CHECK_FALSE(in(names, "c.txt"));
    CHECK_FALSE(in(names, ".hidden"));
    fs.dir_close();

    const int lists = fake.lists;
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    CHECK(fake.lists == lists); // from the cache
    names = names_of(fs);
    CHECK(in(names, "c.txt"));
    CHECK_FALSE(in(names, ".hidden"));

    REQUIRE(fs.dir_open("/games", nullptr, 0).is_success());
    CHECK(fake.lists == lists + 1);
    CHECK(fs.dir_read() == nullptr);

    CHECK(fs.dir_open("/nowhere", nullptr, 0).is_error());
    CHECK(fs.dir_open("/c.txt", nullptr, 0).is_error());
}

TEST_CASE("fs: a change to the volume is seen by the next listing")
{
    Mounted m;
    m.add_file("/a.atr", "1");
    FileSystemAFP fs;
    REQUIRE(fs.start(mount_url).is_success());
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    CHECK(in(names_of(fs), "a.atr"));

    REQUIRE(fs.rename("/a.atr", "/b.atr").is_success());
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    std::vector<std::string> names = names_of(fs);
    CHECK(in(names, "b.atr"));
    CHECK_FALSE(in(names, "a.atr"));

    FileHandler *f = fs.filehandler_open("/new.atr", "w");
    REQUIRE(f != nullptr);
    CHECK(f->close() == 0);
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    CHECK(in(names_of(fs), "new.atr"));

    REQUIRE(fs.remove("/new.atr").is_success());
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    CHECK_FALSE(in(names_of(fs), "new.atr"));
}

TEST_CASE("fs: entries carry size and kind; seek and tell walk the listing")
{
    Mounted m;
    m.add_file("/one.bin", "12345");
    REQUIRE(afp_mkdir(m.session, "/two") == 0);
    FileSystemAFP fs;
    REQUIRE(fs.start(mount_url).is_success());
    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());

    uint16_t start = fs.dir_tell();
    fsdir_entry *first = fs.dir_read();
    REQUIRE(first != nullptr);
    std::string first_name = first->filename;
    fsdir_entry *second = fs.dir_read();
    REQUIRE(second != nullptr);
    fsdir_entry *file = first->isDir ? second : first;
    fsdir_entry *dir = first->isDir ? first : second;
    CHECK(std::string(file->filename) == "one.bin");
    CHECK(file->size == 5);
    CHECK_FALSE(file->isDir);
    CHECK(std::string(dir->filename) == "two");
    CHECK(dir->isDir);

    CHECK(fs.dir_seek(start).is_success());
    fsdir_entry *again = fs.dir_read();
    REQUIRE(again != nullptr);
    CHECK(std::string(again->filename) == first_name);
}

TEST_CASE("fs: a mount below the volume root keeps every path below it")
{
    Mounted m;
    REQUIRE(afp_mkdir(m.session, "/disks") == 0);
    m.add_file("/disks/a.atr", "A");
    m.add_file("/top.txt", "T");
    FileSystemAFP fs;
    REQUIRE(fs.start("afp://user:pw@host/Alpha/disks/").is_success());

    CHECK(fs.exists("/a.atr"));
    CHECK(fs.exists("a.atr"));
    CHECK_FALSE(fs.exists("/top.txt"));
    CHECK(fs.is_dir("/"));
    CHECK(fs.rename("/a.atr", "/b.atr").is_success());
    CHECK(m.has("/disks/b.atr"));

    REQUIRE(fs.dir_open("/", nullptr, 0).is_success());
    std::vector<std::string> names = names_of(fs);
    CHECK(names == std::vector<std::string>{"b.atr"});

    FileHandler *f = fs.filehandler_open("/b.atr", "r");
    REQUIRE(f != nullptr);
    char c = 0;
    CHECK(f->read(&c, 1, 1) == 1);
    CHECK(c == 'A');
    CHECK(f->close() == 0);

    CHECK(fs.mkdir("/sub").is_success());
    CHECK(m.has("/disks/sub"));
    CHECK(fs.remove("/sub").is_success());
    CHECK(fs.remove("/b.atr").is_success());
    CHECK_FALSE(m.has("/disks/b.atr"));
    CHECK(m.has("/top.txt"));
}

TEST_CASE("fs: opening a file that is not there gives no handler")
{
    fake.reset();
    FileSystemAFP fs;
    REQUIRE(fs.start(mount_url).is_success());
    CHECK(fs.filehandler_open("/a.txt", "r") == nullptr);
    CHECK(fs.filehandler_open(nullptr, "r") == nullptr);
    CHECK(fs.file_open("/a.txt", "r") == nullptr);

    FileHandler *f = fs.filehandler_open("/a.txt", "w");
    REQUIRE(f != nullptr);
    CHECK(f->write("hi", 1, 2) == 2);
    CHECK(f->close() == 0);
    CHECK(fs.exists("/a.txt"));

    f = fs.filehandler_open("/a.txt", "r");
    REQUIRE(f != nullptr);
    CHECK(FileSystem::filesize(f) == 2);
    CHECK(f->close() == 0);
}
