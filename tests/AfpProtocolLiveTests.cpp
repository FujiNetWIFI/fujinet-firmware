#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "NetworkProtocolAFP.h"
#include "utils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include <unistd.h>

// The same AFP_TEST_* variables as AfpSessionLiveTests.cpp.  Everything is
// done inside one directory on the volume, /fujinet-afp-n-<pid>, which the
// last case empties and removes; nothing else is touched.
static std::string env(const char *name)
{
    const char *v = std::getenv(name);
    return v ? v : "";
}

static const std::string host = env("AFP_TEST_HOST");
static std::string user = env("AFP_TEST_USER");
static std::string password = env("AFP_TEST_PASSWORD");
static const std::string volume = env("AFP_TEST_VOLUME");
static std::string user2 = env("AFP_TEST_USER2");
static std::string password2 = env("AFP_TEST_PASSWORD2");

static const std::string scratch = "fujinet-afp-n-" + std::to_string(static_cast<long>(getpid()));

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
    return context.run();
}

// One N: unit, logged in the way the device does it: through login and password.
struct Unit
{
    std::string rx, tx, sp;
    NetworkProtocolAFP p{&rx, &tx, &sp};
    std::unique_ptr<PeoplesUrlParser> url;

    Unit(std::string *u = &user, std::string *pw = &password)
    {
        p.login = u;
        p.password = pw;
    }

    PeoplesUrlParser *parse(const std::string &path)
    {
        url = PeoplesUrlParser::parseURL("AFP://" + host + path);
        return url.get();
    }

    fujiError_t open(const std::string &path, fileAccessMode_t mode,
                     netProtoTranslation_t translate = NETPROTO_TRANS::NONE)
    {
        return p.open(parse(path), mode, translate);
    }

    // Reads in pieces of chunk bytes until the protocol reports no more.
    std::string read_all(unsigned short chunk)
    {
        std::string out;
        for (;;)
        {
            size_t want = std::min<size_t>(chunk, p.available());
            if (want == 0 || p.read(want) != FUJI_ERROR::NONE)
                break;
            out += rx;
            rx.clear();
        }
        return out;
    }

    fujiError_t write(const std::string &data)
    {
        tx = data;
        return p.write(data.size());
    }

    // A directory in the RAW format: one name per line, directories with a '/'.
    std::string list(const std::string &path)
    {
        if (open(path, ACCESS_MODE::DIRECTORY, static_cast<netProtoTranslation_t>(DIR_FORMAT::RAW)) !=
            FUJI_ERROR::NONE)
            return "<error>";
        std::string out = read_all(256);
        p.close();
        return out;
    }

    std::string contents(const std::string &path)
    {
        if (open(path, ACCESS_MODE::READ) != FUJI_ERROR::NONE)
            return "<error>";
        std::string out = read_all(256);
        p.close();
        return out;
    }
};

static const std::string vol = "/" + volume;
static const std::string dir = vol + "/" + scratch;
static const std::string eol = "\x9b";

static bool has_line(const std::string &listing, const std::string &line)
{
    return (eol + listing).find(eol + line + eol) != std::string::npos;
}

TEST_CASE("the server with no volume lists its volumes as directories")
{
    Unit n;
    CHECK(has_line(n.list("/"), volume + "/"));
    CHECK(has_line(n.list("/*.*"), volume + "/"));
    // A volume named without a trailing slash is an entry of the server.
    CHECK(has_line(n.list(vol), volume + "/"));
}

TEST_CASE("a file cannot be opened where there is no volume")
{
    Unit n;
    CHECK(n.open("/", ACCESS_MODE::READ) != FUJI_ERROR::NONE);
    CHECK(n.open("/" + volume, ACCESS_MODE::READ) != FUJI_ERROR::NONE);
}

TEST_CASE("mkdir makes a directory once")
{
    Unit n;
    REQUIRE(n.p.mkdir(n.parse(dir)) == FUJI_ERROR::NONE);
    CHECK(has_line(n.list(vol + "/"), scratch + "/"));
    CHECK(has_line(n.list(vol + "/*.*"), scratch + "/"));

    CHECK(n.p.mkdir(n.parse(dir)) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::FILE_EXISTS);
}

TEST_CASE("write, then read back in pieces that do not divide the size")
{
    std::string data;
    for (int i = 0; i < 1000; i++)
        data += static_cast<char>('A' + i % 26);

    Unit n;
    REQUIRE(n.open(dir + "/data.txt", ACCESS_MODE::WRITE) == FUJI_ERROR::NONE);
    CHECK(n.write(data.substr(0, 600)) == FUJI_ERROR::NONE);
    CHECK(n.write(data.substr(600)) == FUJI_ERROR::NONE);
    CHECK(n.p.close() == FUJI_ERROR::NONE);

    REQUIRE(n.open(dir + "/data.txt", ACCESS_MODE::READ) == FUJI_ERROR::NONE);
    CHECK(n.p.available() == 1000);
    CHECK(n.read_all(256) == data);

    NetworkStatus status;
    CHECK(n.p.status(&status) == FUJI_ERROR::NONE);
    CHECK(status.error == NDEV_STATUS::END_OF_FILE);
    CHECK(n.p.close() == FUJI_ERROR::NONE);

    CHECK(has_line(n.list(dir + "/"), "data.txt"));
}

TEST_CASE("write replaces, append continues")
{
    Unit n;
    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::WRITE) == FUJI_ERROR::NONE);
    CHECK(n.write("a long first version") == FUJI_ERROR::NONE);
    n.p.close();
    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::WRITE) == FUJI_ERROR::NONE);
    CHECK(n.write("short") == FUJI_ERROR::NONE);
    n.p.close();
    CHECK(n.contents(dir + "/grow.txt") == "short");

    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::APPEND) == FUJI_ERROR::NONE);
    CHECK(n.write("+more") == FUJI_ERROR::NONE);
    n.p.close();
    CHECK(n.contents(dir + "/grow.txt") == "short+more");
}

TEST_CASE("update seeks and writes in place")
{
    Unit n;
    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::READWRITE) == FUJI_ERROR::NONE);
    CHECK(n.p.seek(2, SEEK_SET) == 2);
    CHECK(n.write("XY") == FUJI_ERROR::NONE);
    n.p.close();
    CHECK(n.contents(dir + "/grow.txt") == "shXYt+more");

    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::READ) == FUJI_ERROR::NONE);
    CHECK(n.p.seek(6, SEEK_SET) == 6);
    CHECK(n.read_all(256) == "more");
    n.p.close();
}

TEST_CASE("seek counts from the end and stays inside the file")
{
    Unit n;
    REQUIRE(n.open(dir + "/grow.txt", ACCESS_MODE::READ) == FUJI_ERROR::NONE);
    CHECK(n.p.seek(4, SEEK_END) == 6);
    CHECK(n.read_all(256) == "more");

    CHECK(n.p.seek(-5, SEEK_SET) == 0);
    CHECK(n.p.available() == 10);
    CHECK(n.p.seek(3, SEEK_SET) == 3);
    CHECK(n.p.seek(-9, SEEK_CUR) == 0);
    CHECK(n.read_all(256) == "shXYt+more");

    CHECK(n.p.seek(100, SEEK_SET) == 100);
    CHECK(n.p.available() == 0);
    CHECK(n.p.seek(100, SEEK_END) == 0);
    CHECK(n.p.available() == 10);
    n.p.close();
}

TEST_CASE("a file is found by its crunched name")
{
    Unit n;
    REQUIRE(n.open(dir + "/A Long File Name.text", ACCESS_MODE::WRITE) == FUJI_ERROR::NONE);
    CHECK(n.write("found") == FUJI_ERROR::NONE);
    n.p.close();

    const std::string crunched = util_crunch_platform("A Long File Name.text");
    REQUIRE(crunched != "A Long File Name.text");
    CHECK(n.contents(dir + "/" + crunched) == "found");
    CHECK(n.p.del(n.parse(dir + "/A Long File Name.text")) == FUJI_ERROR::NONE);
}

TEST_CASE("rename moves the name; the old one is then not found")
{
    Unit n;
    CHECK(n.p.rename(n.parse(dir + "/grow.txt,grown.txt")) == FUJI_ERROR::NONE);
    CHECK(n.contents(dir + "/grown.txt") == "shXYt+more");

    CHECK(n.open(dir + "/grow.txt", ACCESS_MODE::READ) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::FILE_NOT_FOUND);
}

TEST_CASE("two accounts on one server at once")
{
    if (user2.empty())
        return;

    Unit one;
    Unit two(&user2, &password2);
    REQUIRE(one.open(dir + "/data.txt", ACCESS_MODE::READ) == FUJI_ERROR::NONE);
    CHECK(has_line(two.list("/"), volume + "/"));
    CHECK(one.read_all(256).size() == 1000);
    one.p.close();
}

TEST_CASE("credentials in the URL are used before the device's login")
{
    std::string wrong_user = "nobody-here", wrong_password = "wrong";
    Unit n(&wrong_user, &wrong_password);
    n.url = PeoplesUrlParser::parseURL("AFP://" + user + ":" + password + "@" + host + dir + "/data.txt");
    REQUIRE(n.p.open(n.url.get(), ACCESS_MODE::READ, NETPROTO_TRANS::NONE) == FUJI_ERROR::NONE);
    n.p.close();
}

TEST_CASE("no login at all fails without a crash")
{
    Unit n(nullptr, nullptr);
    CHECK(n.open(dir + "/data.txt", ACCESS_MODE::READ) != FUJI_ERROR::NONE);
}

TEST_CASE("a wrong password is access denied")
{
    std::string wrong = password + "-wrong";
    Unit n(&user, &wrong);
    CHECK(n.open(dir + "/data.txt", ACCESS_MODE::READ) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::ACCESS_DENIED);
}

TEST_CASE("an unknown volume is file not found")
{
    Unit n;
    CHECK(n.open("/no-such-volume-here/x.txt", ACCESS_MODE::READ) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::FILE_NOT_FOUND);
}

TEST_CASE("a host that does not resolve is network unreachable")
{
    Unit n;
    n.url = PeoplesUrlParser::parseURL("AFP://no-such-host.invalid/" + volume + "/x.txt");
    CHECK(n.p.open(n.url.get(), ACCESS_MODE::READ, NETPROTO_TRANS::NONE) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::NETWORK_UNREACHABLE);
}

TEST_CASE("delete and rmdir leave nothing behind")
{
    Unit n;
    CHECK(n.p.rmdir(n.parse(dir)) != FUJI_ERROR::NONE);

    // Present only when an earlier case failed part way.
    n.p.del(n.parse(dir + "/grow.txt"));
    n.p.del(n.parse(dir + "/A Long File Name.text"));

    CHECK(n.p.del(n.parse(dir + "/data.txt")) == FUJI_ERROR::NONE);
    CHECK(n.p.del(n.parse(dir + "/grown.txt")) == FUJI_ERROR::NONE);
    CHECK(n.p.del(n.parse(dir + "/data.txt")) != FUJI_ERROR::NONE);
    CHECK(n.p.error == NDEV_STATUS::FILE_NOT_FOUND);

    CHECK(n.list(dir + "/") == "");
    CHECK(n.p.rmdir(n.parse(dir)) == FUJI_ERROR::NONE);
    CHECK_FALSE(has_line(n.list(vol + "/"), scratch + "/"));
}
