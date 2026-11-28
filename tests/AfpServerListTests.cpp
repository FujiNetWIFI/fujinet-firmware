#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
struct afp_server;
struct afp_server *helper_connect(int port);
void helper_log_in(struct afp_server *s, const char *user, const char *signature);
int helper_connect_again(struct afp_server *s);
int helper_list_length(int limit);
struct afp_server *helper_find_by_address(int port, const char *user);
struct afp_server *helper_find_by_signature(const char *signature, const char *user);
void helper_start_loop(void);
struct afp_server *helper_failed_login(int port);
int helper_still_listed(struct afp_server *s);
int helper_remove(struct afp_server *s);
int helper_write_to_readonly_volume(void);
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

TEST_CASE("reconnecting a listed server does not list it twice")
{
    int port = 0;
    int listener = listen_on_loopback(port);
    afp_server *frank = helper_connect(port);
    REQUIRE(frank != nullptr);
    const int before = helper_list_length(1000);
    REQUIRE(before > 0);

    REQUIRE(helper_connect_again(frank) == 0);

    CHECK(helper_list_length(1000) == before);
    close(listener);
}

TEST_CASE("a write is checked against the volume, not rejected outright")
{
    CHECK(helper_write_to_readonly_volume() == -EACCES);
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

static bool read_all(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len)
    {
        ssize_t n = read(fd, buf + got, len - got);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
    }
    return true;
}

// Accepts one connection and answers its DSIOpenSession, then holds it open.
static void answer_open_session(int listener, std::atomic<int> &accepted)
{
    int fd = accept(listener, nullptr, nullptr);
    accepted = fd;
    if (fd < 0) return;

    uint8_t request[16];
    if (!read_all(fd, request, sizeof(request))) return;
    uint32_t length = (uint32_t(request[8]) << 24) | (uint32_t(request[9]) << 16) |
                      (uint32_t(request[10]) << 8) | request[11];
    std::vector<uint8_t> body(length);
    if (length && !read_all(fd, body.data(), length)) return;

    // Reply header, then one option: server request quantum, 4 bytes.
    uint8_t reply[22] = {0};
    reply[0] = 1;
    reply[1] = request[1];
    reply[2] = request[2];
    reply[3] = request[3];
    reply[11] = 6;
    reply[17] = 4;
    reply[20] = 0x10;
    (void)!write(fd, reply, sizeof(reply));
}

TEST_CASE("a server whose login failed is still the caller's to remove")
{
    helper_start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int port = 0;
    int listener = listen_on_loopback(port);
    std::atomic<int> accepted{-1};
    std::thread responder(answer_open_session, listener, std::ref(accepted));

    afp_server *s = helper_failed_login(port);
    REQUIRE(s != nullptr);
    const bool listed = helper_still_listed(s) == 1;
    CHECK(listed);
    if (listed)
    {
        CHECK(helper_remove(s) == 0);
        CHECK(helper_still_listed(s) == 0);
    }

    responder.join();
    if (accepted >= 0) close(accepted);
    close(listener);
}
