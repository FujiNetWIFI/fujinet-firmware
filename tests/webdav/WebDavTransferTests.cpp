// lib/http/webdav: files move through /dav on the ESP32 server (#1839). The
// server is the checkout's own code over Meatloaf's FlashMFile, built by
// webdav_transfer_test.py; WEBDAV_TEST_ROOT is the directory it serves.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fake_httpd.h"

#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

void webdav_register(httpd_handle_t server, const char *root_uri, const char *root_path);

static const std::string CONFIG =
    "[General]\ndevicename=FujiNet\nhsioindex=8\n\n[Host1]\ntype=SD\nname=SD\n";

static std::string root()
{
    static std::string dir;
    if (dir.empty())
    {
        const char *env = std::getenv("WEBDAV_TEST_ROOT");
        REQUIRE_MESSAGE(env != nullptr, "WEBDAV_TEST_ROOT is not set");
        dir = env;
        std::ofstream(dir + "/fnconfig.ini", std::ios::binary) << CONFIG;
        webdav_register(nullptr, "/dav", dir.c_str());
    }
    return dir;
}

static std::string slurp(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// Bodies are compared through a bool so a failure does not print 40 KB of bytes.
static bool same(const std::string &a, const std::string &b) { return a == b; }

static std::string random_bytes(size_t n)
{
    std::string s(n, '\0');
    std::mt19937 rng(1839);
    for (auto &c : s)
        c = (char)(rng() & 0xff);
    return s;
}

TEST_CASE("GET of an existing file returns its contents")
{
    root();
    Exchange e;
    CHECK(request(HTTP_GET, "/dav/fnconfig.ini", e) == 200);
    CHECK(e.resp_body == CONFIG);
}

TEST_CASE("PUT stores a file, GET reads it back, PUT again replaces it")
{
    std::string dir = root();
    std::string big = random_bytes(40000);
    {
        Exchange e;
        e.req_body = big;
        int status = request(HTTP_PUT, "/dav/upload.bin", e);
        CHECK((status == 201 || status == 200));
        CHECK(same(slurp(dir + "/upload.bin"), big));
    }
    {
        Exchange e;
        CHECK(request(HTTP_GET, "/dav/upload.bin", e) == 200);
        CHECK(same(e.resp_body, big));
    }
    {
        std::string smaller = big.substr(0, 1000);
        Exchange e;
        e.req_body = smaller;
        CHECK(request(HTTP_PUT, "/dav/upload.bin", e) == 200);
        CHECK(same(slurp(dir + "/upload.bin"), smaller));
    }
}

TEST_CASE("GET of a missing file is 404")
{
    root();
    Exchange e;
    CHECK(request(HTTP_GET, "/dav/missing.ini", e) == 404);
}

TEST_CASE("Depth 0 PROPFIND on a file reports its size")
{
    root();
    std::string body = random_bytes(40000);
    {
        Exchange e;
        e.req_body = body;
        request(HTTP_PUT, "/dav/sized.bin", e);
    }
    Exchange e;
    e.req_headers["depth"] = "0";
    CHECK(request(HTTP_PROPFIND, "/dav/sized.bin", e) == 207);
    CHECK(e.resp_body.find("<D:getcontentlength>40000</D:getcontentlength>") != std::string::npos);
}
