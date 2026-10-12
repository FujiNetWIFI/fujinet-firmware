#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "afpSession.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

// AFP_TEST_HOST, AFP_TEST_USER, AFP_TEST_PASSWORD and AFP_TEST_VOLUME name a
// real server.  Nothing is written to it.
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
