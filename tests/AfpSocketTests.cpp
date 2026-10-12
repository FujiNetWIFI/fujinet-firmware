#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cerrno>

extern "C" {
#include "afp_socket.h"
}

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

// The library decides what went wrong from errno, on every platform.
TEST_CASE("a failed read reports why through errno")
{
    char buf[4];
    errno = 0;
    CHECK(afp_sock_read(-1, buf, sizeof(buf)) < 0);
    CHECK(errno != 0);
}

TEST_CASE("a failed write reports why through errno")
{
    errno = 0;
    CHECK(afp_sock_write(-1, "x", 1) < 0);
    CHECK(errno != 0);
}

#ifndef _WIN32
TEST_CASE("writing after the server hung up fails with EPIPE instead of killing the process")
{
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    afp_sock_prepare(sv[0]);
    close(sv[1]);

    errno = 0;
    CHECK(afp_sock_write(sv[0], "x", 1) < 0);
    CHECK(errno == EPIPE);
    afp_sock_close(sv[0]);
}
#endif
