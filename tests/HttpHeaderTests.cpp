#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include "fn_http_header.h"

using namespace fujinet;

// ─────────────────────────────────────────────────────────────────────────────
// http_header_generate_string() serialises the request headers in buffer-sized
// chunks and will not split a single item. Getting that boundary wrong is not a
// visible error: the caller stops writing and the blank line terminating the
// header block never goes out, so the server reads the request body as more
// headers. That is FujiNetWIFI/fujinet-firmware#1554 - a Microsoft Graph bearer
// token is 1.2-2.5 KB and never fit the 512-byte default buffer, so every
// OneDrive request went out malformed (GETs hung to timeout, PUTs drew a 400).
//
// These pin the contract the client relies on to size its buffer correctly.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{

// Stand-in for a Graph access token: far larger than the 512-byte default.
std::string big_token()
{
    return std::string(2000, 'A');
}

// Drive the chunked serialisation the way esp_http_client_request_send() does,
// concatenating every chunk. Returns false if the headers do not fit at all.
bool generate_all(http_header_handle_t h, int buffer_size, std::string &out)
{
    std::string buf(buffer_size + 1, '\0');
    int index = 0;
    out.clear();

    for (;;)
    {
        int wlen = buffer_size;
        int ret = http_header_generate_string(h, index, &buf[0], &wlen);

        if (ret == 0)
            return true; // nothing left to write
        if (ret < 0)
            return false; // one item is too large for this buffer

        // The client writes a NUL at data[wlen], so this must stay in bounds.
        REQUIRE(wlen > 0);
        REQUIRE(wlen <= buffer_size - 3);
        REQUIRE(ret > index); // progress, or the caller would spin

        out.append(&buf[0], wlen);
        index = ret;
    }
}

int count_occurrences(const std::string &haystack, const std::string &needle)
{
    int n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + 1))
        n++;
    return n;
}

} // namespace

TEST_CASE("an oversized single header is reported, not silently truncated")
{
    http_header_handle_t h = http_header_init();
    http_header_set(h, "User-Agent", "ESP32 HTTP Client/1.0");
    http_header_set(h, "Host", "graph.microsoft.com");
    http_header_set(h, "Authorization", ("Bearer " + big_token()).c_str());

    // The two small headers fit, so the first call makes progress.
    std::string buf(513, '\0');
    int wlen = 512;
    int ret = http_header_generate_string(h, 0, &buf[0], &wlen);
    CHECK(ret == 2);
    CHECK(wlen > 0);

    // The next call cannot place Authorization. It must say so rather than
    // return the caller's own index with nothing written, which the client
    // could not tell apart from "done".
    wlen = 512;
    ret = http_header_generate_string(h, 2, &buf[0], &wlen);
    CHECK(ret == -1);
    CHECK(wlen == 0);

    http_header_destroy(h);
}

TEST_CASE("an oversized header at index 0 is reported too")
{
    // The client's write loop tests the return value, so a 0 here would exit it
    // before anything - not even the request line - reached the wire.
    http_header_handle_t h = http_header_init();
    http_header_set(h, "Authorization", ("Bearer " + big_token()).c_str());

    std::string buf(513, '\0');
    int wlen = 512;
    CHECK(http_header_generate_string(h, 0, &buf[0], &wlen) == -1);
    CHECK(wlen == 0);

    http_header_destroy(h);
}

TEST_CASE("a buffer sized from the longest item serialises completely")
{
    http_header_handle_t h = http_header_init();
    http_header_set(h, "User-Agent", "ESP32 HTTP Client/1.0");
    http_header_set(h, "Host", "graph.microsoft.com");
    http_header_set(h, "Authorization", ("Bearer " + big_token()).c_str());
    http_header_set(h, "Content-Type", "application/octet-stream");
    http_header_set(h, "Content-Length", "65536");

    // This is exactly how the client sizes its TX buffer.
    int buffer_size = http_header_longest_item_length(h) + 8;

    std::string out;
    REQUIRE(generate_all(h, buffer_size, out));

    // Every header present exactly once, and the header block terminated.
    CHECK(count_occurrences(out, "Host: graph.microsoft.com\r\n") == 1);
    CHECK(count_occurrences(out, "Content-Length: 65536\r\n") == 1);
    CHECK(count_occurrences(out, "Bearer " + big_token()) == 1);
    CHECK(out.size() >= 4);
    CHECK(out.compare(out.size() - 4, 4, "\r\n\r\n") == 0);
    CHECK(count_occurrences(out, "\r\n\r\n") == 1);

    http_header_destroy(h);
}

TEST_CASE("many small headers still split across buffers")
{
    // The multi-chunk path is the normal case; the -1 contract must not disturb it.
    http_header_handle_t h = http_header_init();
    for (int i = 0; i < 40; i++)
        http_header_set(h, ("X-Header-" + std::to_string(i)).c_str(),
                        std::string(40, 'v').c_str());

    std::string out;
    REQUIRE(generate_all(h, 512, out));

    for (int i = 0; i < 40; i++)
        CHECK(count_occurrences(out, "X-Header-" + std::to_string(i) + ": ") == 1);
    CHECK(count_occurrences(out, "\r\n\r\n") == 1);
    CHECK(out.compare(out.size() - 4, 4, "\r\n\r\n") == 0);

    http_header_destroy(h);
}

TEST_CASE("longest item length matches the on-wire form")
{
    http_header_handle_t h = http_header_init();
    CHECK(http_header_longest_item_length(h) == 0);

    http_header_set(h, "Host", "example.com");
    // "Host" + ": " + "example.com" + "\r\n"
    CHECK(http_header_longest_item_length(h) == 4 + 2 + 11 + 2);

    http_header_set(h, "X-Short", "v");
    CHECK(http_header_longest_item_length(h) == 19);

    // A key with no value is never emitted, so it must not count.
    http_header_set(h, "X-Unset", nullptr);
    CHECK(http_header_longest_item_length(h) == 19);

    http_header_destroy(h);
}

TEST_CASE("an empty header list writes nothing")
{
    http_header_handle_t h = http_header_init();

    std::string buf(513, '\0');
    int wlen = 512;
    CHECK(http_header_generate_string(h, 0, &buf[0], &wlen) == 0);

    http_header_destroy(h);
}
