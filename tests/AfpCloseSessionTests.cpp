#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
struct afp_server;
void cs_start_loop(void);
int cs_unmount_count(void);
struct afp_server *cs_connect(int port);
void cs_fake_mounted_login(struct afp_server *s);
int cs_open_session(struct afp_server *s);
unsigned int cs_tx_quantum(struct afp_server *s);
int cs_listed(struct afp_server *s);
int cs_anything_mounted(struct afp_server *s);
int cs_logged_in(struct afp_server *s);
void cs_reap(void);
}

// Threads here may be left blocked inside the library, so never join at exit.
int main(int argc, char **argv)
{
    doctest::Context context(argc, argv);
    int result = context.run();
    std::fflush(nullptr);
    std::_Exit(result);
}

static int listen_on_loopback(int &port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0);
    REQUIRE(listen(fd, 8) == 0);
    socklen_t len = sizeof(sa);
    REQUIRE(getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) == 0);
    port = ntohs(sa.sin_port);
    return fd;
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

// A server that accepts one client and, when told, ends the session.
static void close_session_when_told(int listener, std::atomic<bool> &go)
{
    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) return;
    while (!go) std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t close_session[16] = {0};
    close_session[1] = 1; // DSICloseSession, as a request
    (void)!write(fd, close_session, sizeof(close_session));
    std::this_thread::sleep_for(std::chrono::seconds(30));
}

// A server that answers DSIOpenSession and holds the connection.
static void answer_open_session(int listener)
{
    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) return;

    uint8_t request[16];
    if (!read_all(fd, request, sizeof(request))) return;
    uint32_t length = (uint32_t(request[8]) << 24) | (uint32_t(request[9]) << 16) |
                      (uint32_t(request[10]) << 8) | request[11];
    std::vector<uint8_t> body(length);
    if (length && !read_all(fd, body.data(), length)) return;

    uint8_t reply[22] = {0};
    reply[0] = 1;
    reply[1] = request[1];
    reply[2] = request[2];
    reply[3] = request[3];
    reply[11] = 6;
    reply[17] = 4;
    reply[20] = 0x10;
    (void)!write(fd, reply, sizeof(reply));
    std::this_thread::sleep_for(std::chrono::seconds(30));
}

template <typename Done> static bool within(std::chrono::seconds limit, Done done)
{
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done())
    {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

TEST_CASE("the server ending a session unmounts its volumes and leaves the event loop running")
{
    cs_start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int port = 0;
    int listener = listen_on_loopback(port);
    std::atomic<bool> go{false};
    std::thread(close_session_when_told, listener, std::ref(go)).detach();

    afp_server *victim = cs_connect(port);
    REQUIRE(victim != nullptr);
    cs_fake_mounted_login(victim);

    go = true;
    REQUIRE(within(std::chrono::seconds(10), [] { return cs_unmount_count() == 1; }));

    // The loop must still serve another connection.
    int other_port = 0;
    int other_listener = listen_on_loopback(other_port);
    std::thread(answer_open_session, other_listener).detach();
    afp_server *other = cs_connect(other_port);
    REQUIRE(other != nullptr);
    std::thread([other] { cs_open_session(other); }).detach();
    CHECK(within(std::chrono::seconds(10), [other] { return cs_tx_quantum(other) == 4096; }));

    // The dead login is kept for its owner, not reused and not freed by the loop.
    CHECK(cs_listed(victim) == 1);
    CHECK(cs_anything_mounted(victim) == 0);
    CHECK(cs_logged_in(victim) == 0);

    cs_reap();
    CHECK(cs_listed(victim) == 0);
}
