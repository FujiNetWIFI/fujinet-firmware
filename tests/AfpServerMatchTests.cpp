#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "server_match.h"

#include <cstdint>
#include <cstring>

// Ports are opaque to the comparison, so byte order does not matter here.
static sockaddr_in v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    const uint8_t bytes[4] = {a, b, c, d};
    std::memcpy(&sa.sin_addr, bytes, sizeof(bytes));
    sa.sin_port = port;
    return sa;
}

static sockaddr_in6 v6(uint8_t last, uint16_t port)
{
    sockaddr_in6 sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    uint8_t bytes[16] = {0xfd, 0x00};
    bytes[15] = last;
    std::memcpy(&sa.sin6_addr, bytes, sizeof(bytes));
    sa.sin6_port = port;
    return sa;
}

template <typename T> static const sockaddr *sa(const T &s)
{
    return reinterpret_cast<const sockaddr *>(&s);
}

static addrinfo entry(const sockaddr *addr, addrinfo *next)
{
    addrinfo ai;
    std::memset(&ai, 0, sizeof(ai));
    ai.ai_addr = const_cast<sockaddr *>(addr);
    ai.ai_next = next;
    return ai;
}

TEST_CASE("sockaddr: same IPv4 address and port match")
{
    auto a = v4(192, 168, 1, 10, 548);
    auto b = v4(192, 168, 1, 10, 548);
    CHECK(afp_sockaddr_equal(sa(a), sa(b)) == 1);
}

TEST_CASE("sockaddr: padding does not affect the result")
{
    auto a = v4(192, 168, 1, 10, 548);
    auto b = v4(192, 168, 1, 10, 548);
    std::memset(b.sin_zero, 0xAA, sizeof(b.sin_zero));
    CHECK(afp_sockaddr_equal(sa(a), sa(b)) == 1);
}

TEST_CASE("sockaddr: a different port is a different server")
{
    auto a = v4(192, 168, 1, 10, 548);
    auto b = v4(192, 168, 1, 10, 5548);
    CHECK(afp_sockaddr_equal(sa(a), sa(b)) == 0);
}

TEST_CASE("sockaddr: a different address is a different server")
{
    auto a = v4(192, 168, 1, 10, 548);
    auto b = v4(192, 168, 1, 11, 548);
    CHECK(afp_sockaddr_equal(sa(a), sa(b)) == 0);
}

TEST_CASE("sockaddr: IPv6 compares address and port")
{
    auto a = v6(1, 548);
    auto same = v6(1, 548);
    auto other_addr = v6(2, 548);
    auto other_port = v6(1, 5548);
    CHECK(afp_sockaddr_equal(sa(a), sa(same)) == 1);
    CHECK(afp_sockaddr_equal(sa(a), sa(other_addr)) == 0);
    CHECK(afp_sockaddr_equal(sa(a), sa(other_port)) == 0);
}

TEST_CASE("sockaddr: IPv4 never matches IPv6")
{
    auto a = v4(192, 168, 1, 10, 548);
    auto b = v6(1, 548);
    CHECK(afp_sockaddr_equal(sa(a), sa(b)) == 0);
}

TEST_CASE("sockaddr: NULL and unknown families never match")
{
    auto a = v4(192, 168, 1, 10, 548);
    CHECK(afp_sockaddr_equal(nullptr, sa(a)) == 0);
    CHECK(afp_sockaddr_equal(sa(a), nullptr) == 0);
    CHECK(afp_sockaddr_equal(nullptr, nullptr) == 0);

    sockaddr unknown_a, unknown_b;
    std::memset(&unknown_a, 0, sizeof(unknown_a));
    std::memset(&unknown_b, 0, sizeof(unknown_b));
    unknown_a.sa_family = AF_UNSPEC;
    unknown_b.sa_family = AF_UNSPEC;
    CHECK(afp_sockaddr_equal(&unknown_a, &unknown_b) == 0);
}

TEST_CASE("addrinfo: finds the address anywhere in a resolved list")
{
    auto first = v6(1, 548);
    auto second = v4(192, 168, 1, 10, 548);
    addrinfo tail = entry(sa(second), nullptr);
    addrinfo head = entry(sa(first), &tail);

    auto connected = v4(192, 168, 1, 10, 548);
    auto absent = v4(192, 168, 1, 99, 548);
    CHECK(afp_addrinfo_has(&head, sa(connected)) == 1);
    CHECK(afp_addrinfo_has(&head, sa(first)) == 1);
    CHECK(afp_addrinfo_has(&head, sa(absent)) == 0);
}

TEST_CASE("addrinfo: empty list, NULL address and NULL entries")
{
    auto a = v4(192, 168, 1, 10, 548);
    addrinfo tail = entry(sa(a), nullptr);
    addrinfo head = entry(nullptr, &tail);

    CHECK(afp_addrinfo_has(nullptr, sa(a)) == 0);
    CHECK(afp_addrinfo_has(&head, nullptr) == 0);
    CHECK(afp_addrinfo_has(&head, sa(a)) == 1);
}

TEST_CASE("user: exact, case-sensitive match")
{
    CHECK(afp_same_user("alice", "alice") == 1);
    CHECK(afp_same_user("alice", "bob") == 0);
    CHECK(afp_same_user("Alice", "alice") == 0);
    CHECK(afp_same_user("alice", "alic") == 0);
}

TEST_CASE("user: NULL is the guest user")
{
    CHECK(afp_same_user(nullptr, "") == 1);
    CHECK(afp_same_user("", nullptr) == 1);
    CHECK(afp_same_user(nullptr, nullptr) == 1);
    CHECK(afp_same_user("", "alice") == 0);
    CHECK(afp_same_user(nullptr, "alice") == 0);
}

TEST_CASE("login: shared only once it has completed, and only with its user")
{
    CHECK(afp_login_reusable(1, "alice", "alice") == 1);
    CHECK(afp_login_reusable(0, "alice", "alice") == 0);
    CHECK(afp_login_reusable(1, "alice", "bob") == 0);
    CHECK(afp_login_reusable(1, "", nullptr) == 1);
}

TEST_CASE("login: a guest is never handed a server that has not logged in")
{
    CHECK(afp_login_reusable(0, "", "") == 0);
    CHECK(afp_login_reusable(0, nullptr, nullptr) == 0);
}
