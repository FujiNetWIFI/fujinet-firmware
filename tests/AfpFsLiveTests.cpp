#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "fnFsAFP.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

// The same AFP_TEST_* variables as AfpSessionLiveTests.cpp.  Everything is
// done inside one directory on the volume, /fujinet-afp-fs-<pid>, which is
// removed at the end whatever happened; nothing else is touched.
static std::string env(const char *name)
{
    const char *v = std::getenv(name);
    return v ? v : "";
}

static const std::string host = env("AFP_TEST_HOST");
static const std::string user = env("AFP_TEST_USER");
static const std::string password = env("AFP_TEST_PASSWORD");
static const std::string volume = env("AFP_TEST_VOLUME");

static const std::string scratch = "fujinet-afp-fs-" + std::to_string(static_cast<long>(getpid()));
static const std::string url = "afp://" + host + "/" + volume;

// utils.cpp refers to the speech synthesiser, which is not linked here.
int sam(int, char **) { return 0; }

int main(int argc, char **argv)
{
    if (host.empty())
    {
        std::puts("skipped: AFP_TEST_HOST is not set");
        return 77;
    }
    doctest::Context context(argc, argv);
    int result = context.run();

    // Whatever a failed or filtered run left behind; nothing if it went well.
    FileSystemAFP fs;
    if (fs.start(url.c_str(), user.c_str(), password.c_str()).is_success())
    {
        const std::string dir = "/" + scratch;
        for (const char *name : {"/disk.bin", "/note.txt", "/renamed.txt"})
            fs.remove((dir + name).c_str());
        fs.remove(dir.c_str());
    }
    return result;
}

// The volume, mounted the way a host slot would be but with the login passed in.
struct Mount
{
    FileSystemAFP fs;
    explicit Mount(const std::string &where = url)
    {
        REQUIRE(fs.start(where.c_str(), user.c_str(), password.c_str()).is_success());
    }
};

static std::vector<std::string> names_of(FileSystemAFP &fs, const std::string &dir, const char *pattern = nullptr)
{
    std::vector<std::string> names;
    if (fs.dir_open(dir.c_str(), pattern, 0).is_error())
        return {"<error>"};
    for (fsdir_entry *e = fs.dir_read(); e != nullptr; e = fs.dir_read())
        names.push_back(e->filename);
    fs.dir_close();
    return names;
}

static bool in(const std::vector<std::string> &names, const std::string &name)
{
    return std::count(names.begin(), names.end(), name) == 1;
}

static std::string pattern_bytes(size_t n)
{
    std::string data;
    for (size_t i = 0; i < n; i++)
        data += static_cast<char>('a' + i % 26);
    return data;
}

static std::string read_all(FileSystemAFP &fs, const std::string &path, size_t chunk)
{
    FileHandler *f = fs.filehandler_open(path.c_str(), "r");
    if (f == nullptr)
        return "<error>";
    std::string out, buf(chunk, '\0');
    size_t n;
    while ((n = f->read(&buf[0], 1, chunk)) > 0)
        out.append(buf, 0, n);
    f->close();
    return out;
}

static const std::string dir = "/" + scratch;

TEST_CASE("a wrong password does not mount; the right one does")
{
    FileSystemAFP fs;
    const std::string wrong = password + "-wrong";
    CHECK(fs.start(url.c_str(), user.c_str(), wrong.c_str()).is_error());
    CHECK_FALSE(fs.running());
    CHECK(fs.start(url.c_str(), user.c_str(), password.c_str()).is_success());
    CHECK(fs.running());
}

TEST_CASE("a volume the server does not have does not mount")
{
    FileSystemAFP fs;
    const std::string nowhere = "afp://" + host + "/no-such-volume-here";
    CHECK(fs.start(nowhere.c_str(), user.c_str(), password.c_str()).is_error());
    CHECK_FALSE(fs.running());
}

TEST_CASE("mkdir makes the scratch directory")
{
    Mount m;
    REQUIRE(m.fs.mkdir(dir.c_str()).is_success());
    CHECK(m.fs.is_dir(dir.c_str()));
    CHECK(m.fs.dir_exists((dir + "/").c_str()));
    CHECK(m.fs.exists(dir.c_str()));
    CHECK_FALSE(m.fs.exists((dir + "/nothing.bin").c_str()));
    CHECK(m.fs.mkdir(dir.c_str()).is_error());
    CHECK(in(names_of(m.fs, "/"), scratch));
}

TEST_CASE("a file written through a handler reads back the same")
{
    Mount m;
    const std::string data = pattern_bytes(3000);
    const std::string path = dir + "/disk.bin";

    FileHandler *f = m.fs.filehandler_open(path.c_str(), "w");
    REQUIRE(f != nullptr);
    CHECK(f->write(data.data(), 1, 2000) == 2000);
    CHECK(f->write(data.data() + 2000, 1, 1000) == 1000);
    CHECK(f->close() == 0);

    CHECK(read_all(m.fs, path, 512) == data);

    f = m.fs.filehandler_open(path.c_str(), "r");
    REQUIRE(f != nullptr);
    CHECK(FileSystem::filesize(f) == 3000);
    CHECK(f->close() == 0);
}

TEST_CASE("a disk image is changed in place through rb+")
{
    Mount m;
    const std::string path = dir + "/disk.bin";
    std::string expected = pattern_bytes(3000);
    expected.replace(1024, 16, "0123456789ABCDEF");

    FileHandler *f = m.fs.filehandler_open(path.c_str(), "rb+");
    REQUIRE(f != nullptr);
    CHECK(f->seek(1024, SEEK_SET) == 0);
    CHECK(f->write("0123456789ABCDEF", 16, 1) == 1);
    char back[16];
    CHECK(f->seek(-16, SEEK_CUR) == 0);
    CHECK(f->read(back, 1, 16) == 16);
    CHECK(std::string(back, 16) == "0123456789ABCDEF");
    CHECK(f->close() == 0);

    CHECK(read_all(m.fs, path, 700) == expected);
}

TEST_CASE("a listing filters, carries sizes, and follows a rename")
{
    Mount m;
    FileHandler *f = m.fs.filehandler_open((dir + "/note.txt").c_str(), "w");
    REQUIRE(f != nullptr);
    CHECK(f->write("hello", 1, 5) == 5);
    CHECK(f->close() == 0);

    std::vector<std::string> names = names_of(m.fs, dir, "*.bin");
    CHECK(in(names, "disk.bin"));
    CHECK_FALSE(in(names, "note.txt"));

    REQUIRE(m.fs.dir_open(dir.c_str(), nullptr, 0).is_success());
    bool found = false;
    for (fsdir_entry *e = m.fs.dir_read(); e != nullptr; e = m.fs.dir_read())
    {
        if (std::string(e->filename) != "disk.bin")
            continue;
        found = true;
        CHECK(e->size == 3000);
        CHECK_FALSE(e->isDir);
    }
    CHECK(found);

    REQUIRE(m.fs.rename((dir + "/note.txt").c_str(), (dir + "/renamed.txt").c_str()).is_success());
    names = names_of(m.fs, dir);
    CHECK(in(names, "renamed.txt"));
    CHECK_FALSE(in(names, "note.txt"));
    CHECK(names_of(m.fs, dir + "/nowhere") == std::vector<std::string>{"<error>"});
}

TEST_CASE("a mount on the scratch directory sees only what is in it")
{
    Mount m(url + dir);
    CHECK(m.fs.is_dir("/"));
    CHECK(m.fs.exists("/disk.bin"));
    std::vector<std::string> names = names_of(m.fs, "/");
    CHECK(names.size() == 2);
    CHECK(in(names, "disk.bin"));
    CHECK(in(names, "renamed.txt"));
    CHECK(read_all(m.fs, "/renamed.txt", 64) == "hello");
}

TEST_CASE("remove empties the scratch directory and takes it away")
{
    Mount m;
    CHECK(m.fs.rmdir(dir.c_str()).is_error()); // not empty

    // Present only when an earlier case failed part way.
    m.fs.remove((dir + "/note.txt").c_str());

    CHECK(m.fs.remove((dir + "/disk.bin").c_str()).is_success());
    CHECK(m.fs.remove((dir + "/renamed.txt").c_str()).is_success());
    CHECK(m.fs.remove((dir + "/disk.bin").c_str()).is_error());
    CHECK(names_of(m.fs, dir).empty());

    CHECK(m.fs.remove(dir.c_str()).is_success());
    CHECK_FALSE(m.fs.exists(dir.c_str()));
    CHECK_FALSE(in(names_of(m.fs, "/"), scratch));
}
