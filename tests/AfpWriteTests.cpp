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
void wr_prepare(struct afp_server *s, unsigned int quantum, int locking);
int wr_write(struct afp_server *s, const char *data, unsigned int len);
}

// AFP result codes the server below can be told to give.
static const int kNoErr = 0, kAccessDenied = -5000, kDiskFull = -5008, kFileBusy = -5010;

static afp_server *server = nullptr;
static std::atomic<int> writes{0};       // write requests seen
static std::atomic<int> write_result{0}; // what a write is answered with ...
static std::atomic<int> fail_from{1};    // ... from this write on (1 is the first)
static std::atomic<int> lock_result{0};

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

// Answers byte-range locks and writes as told; anything else succeeds.
static void file_server(int listener)
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

        int result = kNoErr;
        const uint8_t command = body.empty() ? 0 : body[0];
        if (command == 59) // afpByteRangeLockExt
            result = lock_result;
        else if (command == 61 && ++writes >= fail_from) // afpWriteExt
            result = write_result;

        // A successful lock or write reply carries one 64-bit value.
        uint8_t reply[24] = {0};
        const uint32_t code = static_cast<uint32_t>(result);
        reply[0] = 1;
        reply[1] = request[1];
        reply[2] = request[2];
        reply[3] = request[3];
        reply[4] = static_cast<uint8_t>(code >> 24);
        reply[5] = static_cast<uint8_t>(code >> 16);
        reply[6] = static_cast<uint8_t>(code >> 8);
        reply[7] = static_cast<uint8_t>(code);
        const size_t size = result == kNoErr ? 24 : 16;
        if (result == kNoErr) reply[11] = 8;
        if (write(fd, reply, size) != static_cast<ssize_t>(size)) return;
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
    std::thread(file_server, fd).detach();

    server = cs_connect(ntohs(sa.sin_port));
    if (server == nullptr) return 2;
    cs_fake_mounted_login(server);

    doctest::Context context(argc, argv);
    int result = context.run();
    std::fflush(nullptr);
    std::_Exit(result);
}

static void server_says(int write_code, int from_write = 1, int lock_code = kNoErr)
{
    writes = 0;
    write_result = write_code;
    fail_from = from_write;
    lock_result = lock_code;
}

TEST_CASE("a write the server accepts returns its length")
{
    wr_prepare(server, 1024, 0);
    server_says(kNoErr);
    CHECK(wr_write(server, "0123456789", 10) == 10);
    CHECK(writes == 1);
}

TEST_CASE("a full disk is reported")
{
    wr_prepare(server, 1024, 0);
    server_says(kDiskFull);
    CHECK(wr_write(server, "0123456789", 10) == -ENOSPC);
}

TEST_CASE("a refused write is reported")
{
    wr_prepare(server, 1024, 0);
    server_says(kAccessDenied);
    CHECK(wr_write(server, "0123456789", 10) == -EACCES);
}

TEST_CASE("an error the library has no name for is still an error")
{
    wr_prepare(server, 1024, 0);
    server_says(kFileBusy);
    CHECK(wr_write(server, "0123456789", 10) == -EIO);
}

TEST_CASE("a write that fails part way returns what was written, and stops")
{
    wr_prepare(server, 4, 0);
    server_says(kDiskFull, 2);
    CHECK(wr_write(server, "0123456789", 10) == 4);
    CHECK(writes == 2);
}

TEST_CASE("a range that cannot be locked is reported, and nothing is written")
{
    wr_prepare(server, 1024, 1);
    server_says(kNoErr, 1, kAccessDenied);
    CHECK(wr_write(server, "0123456789", 10) == -EBUSY);
    CHECK(writes == 0);
}

TEST_CASE("with locking on, an accepted write still returns its length")
{
    wr_prepare(server, 1024, 1);
    server_says(kNoErr);
    CHECK(wr_write(server, "0123456789", 10) == 10);
}
