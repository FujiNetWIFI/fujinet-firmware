#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" {
void lc_start(void);
int lc_login_errno(const char *host, int port);
}

// The library's loop thread is still running, so never join at exit.
int main(int argc, char **argv)
{
    lc_start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    doctest::Context context(argc, argv);
    int result = context.run();
    std::fflush(nullptr);
    std::_Exit(result);
}

// A loopback port with nothing listening on it.
static int closed_port()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0);
    socklen_t len = sizeof(sa);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == 0);
    close(fd);
    return ntohs(sa.sin_port);
}

TEST_CASE("a name that does not resolve is reported as an unreachable host")
{
    CHECK(lc_login_errno("no-such-host.invalid", 548) == EHOSTUNREACH);
}

TEST_CASE("a port nothing listens on is reported as a refused connection")
{
    CHECK(lc_login_errno("127.0.0.1", closed_port()) == ECONNREFUSED);
}
