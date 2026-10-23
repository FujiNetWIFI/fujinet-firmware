#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "SMB.h"
#include "fnFsSMB.h"
#include "samlib.h"
#include "fnTcpClient.h"
#include "compat_inet.h"

// TCP.cpp is linked only because Protocol.cpp casts to it; its harness wants this.
FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

// libsmb2 leaks its wait state on a failed connect (#2154)
extern "C" const char *__lsan_default_suppressions() { return "leak:smb2_connect_share\n"; }

namespace
{

// A local port with nothing listening on it, so a connect is refused at once
int closed_port()
{
#ifdef _WIN32
    WSADATA wsa;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    int s = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(s >= 0);
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(bind(s, (struct sockaddr *)&a, sizeof(a)) == 0);
    socklen_t len = sizeof(a);
    REQUIRE(getsockname(s, (struct sockaddr *)&a, &len) == 0);
    closesocket(s);
    return ntohs(a.sin_port);
}

struct SMB
{
    std::string rx, tx, sp;
    NetworkProtocolSMB proto{&rx, &tx, &sp};
};

} // namespace

// Before #1905 a URL with no share dereferenced the null smb2_url
TEST_CASE("mkdir and rmdir fail on a URL with no share")
{
    SMB smb;
    PeoplesUrlParser url("smb://localhost");
    CHECK(smb.proto.mkdir(&url) != FUJI_ERROR::NONE);
    CHECK(smb.proto.rmdir(&url) != FUJI_ERROR::NONE);
}

// #1780: a failed connect used to report success
TEST_CASE("mkdir and rmdir fail when the server can't be reached")
{
    SMB smb;
    std::string where = "smb://127.0.0.1:" + std::to_string(closed_port()) + "/share/dir/";
    PeoplesUrlParser url(where);
    CHECK(smb.proto.mkdir(&url) != FUJI_ERROR::NONE);
    CHECK(smb.proto.rmdir(&url) != FUJI_ERROR::NONE);
}

// #2135: a failed start() leaked its context; only LeakSanitizer (Linux) sees it
TEST_CASE("FileSystemSMB frees what a failed start() made")
{
    SUBCASE("a URL that isn't smb://")
    {
        FileSystemSMB host;
        CHECK(host.start("ftp://127.0.0.1/share").is_error());
    }

    SUBCASE("a server that can't be reached")
    {
        FileSystemSMB host;
        std::string where = "smb://127.0.0.1:" + std::to_string(closed_port()) + "/share";
        CHECK(host.start(where.c_str()).is_error());
    }
}
