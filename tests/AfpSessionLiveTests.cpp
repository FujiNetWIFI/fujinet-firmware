#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "afpFile.h"
#include "afpSession.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// AFP_TEST_HOST, AFP_TEST_USER, AFP_TEST_PASSWORD and AFP_TEST_VOLUME name a
// real server.  The file cases create and remove one directory on that
// volume, /fujinet-afp-test; nothing else is touched.
static std::string env(const char *name)
{
    const char *v = std::getenv(name);
    return v ? v : "";
}

static const std::string host = env("AFP_TEST_HOST");
static const std::string user = env("AFP_TEST_USER");
static const std::string password = env("AFP_TEST_PASSWORD");
static const std::string volume = env("AFP_TEST_VOLUME");
// Optional second account, for two logins on one server at once.
static const std::string user2 = env("AFP_TEST_USER2");
static const std::string password2 = env("AFP_TEST_PASSWORD2");

int main(int argc, char **argv)
{
    if (host.empty())
    {
        std::puts("skipped: AFP_TEST_HOST is not set");
        return 77;
    }
    doctest::Context context(argc, argv);
    return context.run();
}

TEST_CASE("logs in, attaches the volume and lists it")
{
    AfpSession s;
    REQUIRE(s.open(host, 0, user, password, volume).is_success());
    CHECK(s.volume() != nullptr);
    const auto &names = s.volume_names();
    CHECK(std::find(names.begin(), names.end(), volume) != names.end());
}

TEST_CASE("two sessions share one attachment and survive each other closing")
{
    AfpSession a, b;
    REQUIRE(a.open(host, 0, user, password, volume).is_success());
    REQUIRE(b.open(host, 0, user, password, volume).is_success());
    CHECK(a.volume() == b.volume());
    a.close();
    CHECK(b.volume() != nullptr);
}

TEST_CASE("a session can be opened again after everything was closed")
{
    {
        AfpSession first;
        REQUIRE(first.open(host, 0, user, password, volume).is_success());
    }
    AfpSession second;
    REQUIRE(second.open(host, 0, user, password, volume).is_success());
    CHECK(second.volume() != nullptr);
}

TEST_CASE("no volume: lists the names and attaches nothing")
{
    AfpSession s;
    REQUIRE(s.open(host, 0, user, password, "").is_success());
    CHECK(s.volume() == nullptr);
    CHECK_FALSE(s.volume_names().empty());
}

TEST_CASE("an unknown volume is reported as not found")
{
    AfpSession s;
    CHECK(s.open(host, 0, user, password, "no-such-volume-xyz").is_error());
    CHECK(s.error() == -ENOENT);
}

TEST_CASE("a wrong password is reported as access denied")
{
    AfpSession s;
    CHECK(s.open(host, 0, user, password + "-wrong", volume).is_error());
    CHECK(s.error() == -EACCES);
}

TEST_CASE("a wrong password is refused even while that user's login is in use")
{
    AfpSession holder, intruder;
    REQUIRE(holder.open(host, 0, user, password, volume).is_success());
    CHECK(intruder.open(host, 0, user, password + "-wrong", volume).is_error());
    CHECK(intruder.error() == -EACCES);
    CHECK(intruder.volume() == nullptr);
    CHECK(holder.volume() != nullptr);
}

TEST_CASE("a host that does not resolve is reported as unreachable")
{
    AfpSession s;
    CHECK(s.open("no-such-host.invalid", 0, user, password, volume).is_error());
    CHECK(s.error() == -EHOSTUNREACH);
}

TEST_CASE("two accounts on one server each get their own login")
{
    if (user2.empty())
    {
        MESSAGE("skipped: AFP_TEST_USER2 is not set");
        return;
    }
    AfpSession a, b;
    REQUIRE(a.open(host, 0, user, password, volume).is_success());
    REQUIRE(b.open(host, 0, user2, password2, volume).is_success());
    CHECK(a.volume() != nullptr);
    CHECK(b.volume() != nullptr);
    CHECK(a.volume() != b.volume());
    a.close();
    CHECK(b.volume() != nullptr);
}

// Everything the file cases create lives under this one directory.
static const std::string scratch = "/fujinet-afp-test";

// Removes the scratch directory and what the cases below put in it.
static void clear_scratch(AfpSession &s)
{
    std::vector<AfpDirEntry> entries;
    if (afp_list(s, scratch, entries) != 0)
        return;
    for (const auto &e : entries)
        if (!e.is_dir)
            afp_remove(s, scratch + "/" + e.name);
    afp_rmdir(s, scratch);
}

// A session with an empty scratch directory, removed again on the way out
// even when a case fails.
struct Scratch
{
    AfpSession session;
    Scratch()
    {
        REQUIRE(session.open(host, 0, user, password, volume).is_success());
        clear_scratch(session);
        REQUIRE(afp_mkdir(session, scratch) == 0);
    }
    ~Scratch() { clear_scratch(session); }
};

TEST_CASE("file: write, read back in pieces, and stat")
{
    Scratch t;
    const std::string path = scratch + "/data.bin";
    std::string content;
    for (int i = 0; i < 1200; i++)
        content.push_back(static_cast<char>('A' + i % 26));

    {
        AfpFile f(t.session);
        REQUIRE(f.open(path, AfpOpenMode::Write).is_success());
        REQUIRE(f.write(content.data(), content.size()) == 1200);
        REQUIRE(f.close().is_success());
    }

    AfpDirEntry e;
    REQUIRE(afp_stat(t.session, path, e) == 0);
    CHECK(e.size == 1200);
    CHECK_FALSE(e.is_dir);

    AfpFile f(t.session);
    REQUIRE(f.open(path, AfpOpenMode::Read).is_success());
    CHECK(f.size() == 1200);
    std::string back;
    char buf[512];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
        back.append(buf, static_cast<size_t>(n));
    CHECK(n == 0);
    CHECK(back == content);
}

TEST_CASE("file: write truncates, append continues, update writes in place")
{
    Scratch t;
    const std::string path = scratch + "/modes.txt";
    auto put = [&](const std::string &data, AfpOpenMode mode) {
        AfpFile f(t.session);
        REQUIRE(f.open(path, mode).is_success());
        REQUIRE(f.write(data.data(), data.size()) == static_cast<int>(data.size()));
        REQUIRE(f.close().is_success());
    };
    auto get = [&]() {
        AfpFile f(t.session);
        REQUIRE(f.open(path, AfpOpenMode::Read).is_success());
        char buf[64];
        int n = f.read(buf, sizeof(buf));
        REQUIRE(n >= 0);
        return std::string(buf, static_cast<size_t>(n));
    };

    put("a long first version", AfpOpenMode::Write);
    put("0123456789", AfpOpenMode::Write);
    CHECK(get() == "0123456789");

    put("abc", AfpOpenMode::Append);
    CHECK(get() == "0123456789abc");

    {
        AfpFile f(t.session);
        REQUIRE(f.open(path, AfpOpenMode::Update).is_success());
        f.seek(2);
        REQUIRE(f.write("XY", 2) == 2);
        REQUIRE(f.close().is_success());
    }
    CHECK(get() == "01XY456789abc");
}

TEST_CASE("file: missing files and directories are reported, not invented")
{
    Scratch t;
    AfpFile f(t.session);
    CHECK(f.open(scratch + "/nope.txt", AfpOpenMode::Read).is_error());
    CHECK(f.error() == -ENOENT);
    CHECK(f.open(scratch + "/nowhere/x.txt", AfpOpenMode::Write).is_error());

    AfpDirEntry e;
    CHECK(afp_stat(t.session, scratch + "/nope.txt", e) == -ENOENT);

    std::vector<AfpDirEntry> entries;
    CHECK(afp_list(t.session, scratch + "/nowhere", entries) == -ENOENT);
}

TEST_CASE("directory: list, rename, remove, mkdir and rmdir")
{
    Scratch t;
    {
        AfpFile f(t.session);
        REQUIRE(f.open(scratch + "/one.txt", AfpOpenMode::Write).is_success());
        REQUIRE(f.write("1", 1) == 1);
    }

    std::vector<AfpDirEntry> entries;
    REQUIRE(afp_list(t.session, scratch, entries) == 0);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "one.txt");
    CHECK(entries[0].size == 1);
    CHECK_FALSE(entries[0].is_dir);

    CHECK(afp_list(t.session, scratch + "/one.txt", entries) == -ENOTDIR);

    CHECK(afp_rename(t.session, scratch + "/one.txt", scratch + "/two.txt") == 0);
    REQUIRE(afp_list(t.session, scratch, entries) == 0);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "two.txt");

    CHECK(afp_mkdir(t.session, scratch) == -EEXIST);
    CHECK(afp_rmdir(t.session, scratch) == -ENOTEMPTY);
    CHECK(afp_remove(t.session, scratch + "/two.txt") == 0);
    CHECK(afp_remove(t.session, scratch + "/two.txt") == -ENOENT);

    AfpDirEntry e;
    REQUIRE(afp_stat(t.session, scratch, e) == 0);
    CHECK(e.is_dir);
}

#ifndef _WIN32
TEST_CASE("a port nothing listens on is reported as a refused connection")
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0);
    socklen_t len = sizeof(sa);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == 0);
    close(fd);

    AfpSession s;
    CHECK(s.open("127.0.0.1", ntohs(sa.sin_port), user, password, volume).is_error());
    CHECK(s.error() == -ECONNREFUSED);
}
#endif
