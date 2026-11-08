#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "HTTP.h"
#include "samlib.h"
#include "fnTcpClient.h"

FakeHttpServer fake_http;

// TCP.cpp is linked only because Protocol.cpp casts to it; its harness wants this.
FakePeer fake_peer;

// utils.cpp's speech helper is never called here; the stub keeps SAM out of the link.
int sam(int argc, char **argv) { return 0; }

namespace
{

struct Channel
{
    std::string rx, tx, sp;
    NetworkProtocolHTTP http{&rx, &tx, &sp};
    PeoplesUrlParser url{"HTTP://host/path"};

    explicit Channel(HTTP_METHOD method)
    {
        fake_http = FakeHttpServer();
        auto mode = static_cast<fileAccessMode_t>(static_cast<uint8_t>(method));
        REQUIRE(http.open(&url, mode, NETPROTO_TRANS::NONE) == FUJI_ERROR::NONE);
    }

    void write(const std::string &data)
    {
        tx = data;
        REQUIRE(http.write(data.size()) == FUJI_ERROR::NONE);
    }

    void post_data(const std::string &data)
    {
        REQUIRE(http.set_channel_mode(HTTP_CHANMODE::SET_POST_DATA) == FUJI_ERROR::NONE);
        write(data);
        REQUIRE(http.set_channel_mode(HTTP_CHANMODE::BODY) == FUJI_ERROR::NONE);
    }

    void status()
    {
        NetworkStatus st;
        http.status(&st);
    }
};

} // namespace

// fujinet-lib's network_http_put(): body in SET_POST_DATA, then BODY.
TEST_CASE("a PUT whose body is sent as post data goes out on the first STATUS, once")
{
    Channel ch(HTTP_METHOD::PUT);
    ch.post_data("body");
    ch.status();

    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].method == "PUT");
    CHECK(fake_http.requests[0].body == "body");

    ch.http.close();
    CHECK(fake_http.requests.size() == 1);
}

TEST_CASE("a PUT with nothing before the close goes out at the close")
{
    Channel ch(HTTP_METHOD::PUT);
    ch.post_data("body");
    ch.http.close();

    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].method == "PUT");
    CHECK(fake_http.requests[0].body == "body");
}

// The body written in BODY mode, with a STATUS between writes.
TEST_CASE("a PUT body written in BODY mode goes out whole, at the close")
{
    Channel ch(HTTP_METHOD::PUT);
    ch.write("part1,");
    ch.status();
    ch.write("part2");
    ch.status();
    CHECK(fake_http.requests.empty());

    ch.http.close();
    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].method == "PUT");
    CHECK(fake_http.requests[0].body == "part1,part2");
}

TEST_CASE("a read after a BODY-mode PUT body sends it, and the close does not resend it")
{
    Channel ch(HTTP_METHOD::PUT);
    fake_http.response = "ok";
    ch.write("body");
    ch.http.read(2);

    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].body == "body");

    ch.http.close();
    CHECK(fake_http.requests.size() == 1);
}

TEST_CASE("OPEN_MODE_HTTP_PUT_H sends a PUT with the headers that were set, once")
{
    Channel ch(HTTP_METHOD::PUT_H);
    REQUIRE(ch.http.set_channel_mode(HTTP_CHANMODE::SET_HEADERS) == FUJI_ERROR::NONE);
    ch.write("Content-Type: application/json");
    ch.post_data("{}");
    ch.status();

    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].method == "PUT");
    CHECK(fake_http.requests[0].body == "{}");
    CHECK(fake_http.requests[0].headers["Content-Type"] == "application/json");

    ch.http.close();
    CHECK(fake_http.requests.size() == 1);
}

TEST_CASE("a POST goes out on the first STATUS and not again at the close")
{
    Channel ch(HTTP_METHOD::POST);
    ch.post_data("body");
    ch.status();

    REQUIRE(fake_http.requests.size() == 1);
    CHECK(fake_http.requests[0].method == "POST");
    CHECK(fake_http.requests[0].body == "body");

    ch.http.close();
    CHECK(fake_http.requests.size() == 1);
}
