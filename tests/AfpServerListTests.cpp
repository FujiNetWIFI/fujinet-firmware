#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

extern "C" {
struct afp_server;
struct afp_server *helper_connect(int port);
void helper_log_in(struct afp_server *s, const char *user, const char *signature);
struct afp_server *helper_find_by_address(int port, const char *user);
struct afp_server *helper_find_by_signature(const char *signature, const char *user);
}

// A loopback listener that never accepts: connects complete from its backlog.
static int listen_on_loopback(int &port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    REQUIRE(bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0);
    REQUIRE(listen(fd, 8) == 0);
    socklen_t len = sizeof(sa);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == 0);
    port = ntohs(sa.sin_port);
    return fd;
}

static const char sig_a[16] = {'s', 'e', 'r', 'v', 'e', 'r', '-', 'A', 0, 0, 0, 0, 0, 0, 0, 1};
static const char sig_b[16] = {'s', 'e', 'r', 'v', 'e', 'r', '-', 'B', 0, 0, 0, 0, 0, 0, 0, 2};

TEST_CASE("a connection that has not logged in is never handed out, even to a guest")
{
    int port = 0;
    int listener = listen_on_loopback(port);
    afp_server *probe = helper_connect(port);
    REQUIRE(probe != nullptr);

    CHECK(helper_find_by_address(port, "") == nullptr);
    CHECK(helper_find_by_address(port, nullptr) == nullptr);
    close(listener);
}

TEST_CASE("a login is found by its own user and by nobody else")
{
    int port = 0;
    int listener = listen_on_loopback(port);
    afp_server *alice = helper_connect(port);
    REQUIRE(alice != nullptr);
    helper_log_in(alice, "alice", sig_a);

    CHECK(helper_find_by_address(port, "alice") == alice);
    CHECK(helper_find_by_address(port, "bob") == nullptr);
    CHECK(helper_find_by_address(port, "") == nullptr);
    CHECK(helper_find_by_signature(sig_a, "alice") == alice);
    CHECK(helper_find_by_signature(sig_a, "bob") == nullptr);
    close(listener);
}

TEST_CASE("two users on one server each get their own login")
{
    int port = 0;
    int listener = listen_on_loopback(port);
    afp_server *carol = helper_connect(port);
    afp_server *dave = helper_connect(port);
    REQUIRE(carol != nullptr);
    REQUIRE(dave != nullptr);
    REQUIRE(carol != dave);
    helper_log_in(carol, "carol", sig_b);
    helper_log_in(dave, "dave", sig_b);

    CHECK(helper_find_by_address(port, "carol") == carol);
    CHECK(helper_find_by_address(port, "dave") == dave);
    CHECK(helper_find_by_signature(sig_b, "carol") == carol);
    CHECK(helper_find_by_signature(sig_b, "dave") == dave);
    close(listener);
}

TEST_CASE("the same address on another port is another server")
{
    int port = 0, other_port = 0;
    int listener = listen_on_loopback(port);
    int other = listen_on_loopback(other_port);
    afp_server *erin = helper_connect(port);
    REQUIRE(erin != nullptr);
    helper_log_in(erin, "erin", sig_a);

    CHECK(helper_find_by_address(port, "erin") == erin);
    CHECK(helper_find_by_address(other_port, "erin") == nullptr);
    close(listener);
    close(other);
}
