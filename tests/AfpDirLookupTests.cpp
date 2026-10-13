#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
struct afp_server;
void cs_start_loop(void);
struct afp_server *cs_connect(int port);
void cs_fake_mounted_login(struct afp_server *s);
int dl_creat(struct afp_server *s, const char *path);
int dl_unlink(struct afp_server *s, const char *path);
int dl_mkdir(struct afp_server *s, const char *path);
int dl_rmdir(struct afp_server *s, const char *path);
int dl_rename(struct afp_server *s, const char *from, const char *to);
}

// How many requests the server below has been sent.
static std::atomic<int> requests{0};
static afp_server *server = nullptr;

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

// A server on which nothing exists: every request is answered "object not found".
static void nothing_exists(int listener)
{
    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) return;
    for (;;)
    {
        uint8_t request[16];
        if (!read_all(fd, request, sizeof(request))) return;
        uint32_t length = (uint32_t(request[8]) << 24) | (uint32_t(request[9]) << 16) |
                          (uint32_t(request[10]) << 8) | request[11];
        std::vector<uint8_t> body(length);
        if (length && !read_all(fd, body.data(), length)) return;
        requests++;

        uint8_t reply[16] = {0};
        reply[0] = 1;
        reply[1] = request[1];
        reply[2] = request[2];
        reply[3] = request[3];
        const uint32_t not_found = static_cast<uint32_t>(-5018); // kFPObjectNotFound
        reply[4] = static_cast<uint8_t>(not_found >> 24);
        reply[5] = static_cast<uint8_t>(not_found >> 16);
        reply[6] = static_cast<uint8_t>(not_found >> 8);
        reply[7] = static_cast<uint8_t>(not_found);
        if (write(fd, reply, sizeof(reply)) != static_cast<ssize_t>(sizeof(reply))) return;
    }
}

// The library's loop thread and the server thread are still running, so
// never join at exit.
int main(int argc, char **argv)
{
    cs_start_loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(sa);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0 || listen(fd, 1) != 0 ||
        getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &len) != 0)
        return 2;
    std::thread(nothing_exists, fd).detach();

    server = cs_connect(ntohs(sa.sin_port));
    if (server == nullptr) return 2;
    cs_fake_mounted_login(server);

    doctest::Context context(argc, argv);
    int result = context.run();
    std::fflush(nullptr);
    std::_Exit(result);
}

// Each operation names a parent directory that does not exist.  Finding
// that out takes one request; nothing may be sent after it, because the
// directory ID it would carry was never set.
template <typename Call> static void refuses_after_one_lookup(Call call)
{
    const int before = requests;
    CHECK(call() == -ENOENT);
    CHECK(requests - before == 1);
}

TEST_CASE("create in a missing directory sends nothing after the failed lookup")
{
    refuses_after_one_lookup([] { return dl_creat(server, "/nowhere/file.txt"); });
}

TEST_CASE("remove in a missing directory sends nothing after the failed lookup")
{
    refuses_after_one_lookup([] { return dl_unlink(server, "/nowhere/file.txt"); });
}

TEST_CASE("mkdir in a missing directory sends nothing after the failed lookup")
{
    refuses_after_one_lookup([] { return dl_mkdir(server, "/nowhere/sub"); });
}

TEST_CASE("rmdir in a missing directory sends nothing after the failed lookup")
{
    refuses_after_one_lookup([] { return dl_rmdir(server, "/nowhere/sub"); });
}

TEST_CASE("rename from a missing directory sends nothing after the failed lookup")
{
    refuses_after_one_lookup([] { return dl_rename(server, "/nowhere/a.txt", "/nowhere/b.txt"); });
}
