#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "fnpretty/fnpretty.h"

// Debug_printf() resolves to this; the firmware's implementation lives in
// utils.cpp with the rest of the system layer, which the renderer doesn't need.
void util_debug_printf(const char *fmt, ...) { (void)fmt; }

// Render with LF line endings so expectations read normally here; the Atari
// device sets 0x9B instead.
static std::vector<std::string> render_lines(const std::string &html, uint8_t width)
{
    FNPretty renderer;
    renderer.setLineEnding("\n");
    renderer.setScreenWidth(width);
    REQUIRE(renderer.renderDocument(html));

    std::vector<std::string> lines;
    const std::string &out = renderer.rendered();
    size_t pos = 0;
    while (pos < out.size())
    {
        size_t eol = out.find('\n', pos);
        if (eol == std::string::npos)
        {
            lines.push_back(out.substr(pos));
            break;
        }
        lines.push_back(out.substr(pos, eol - pos));
        pos = eol + 1;
    }
    return lines;
}

TEST_CASE("each block element renders on its own line")
{
    auto lines = render_lines("<html><body><h1>Title</h1><p>First para.</p>"
                              "<ul><li>one</li><li>two</li></ul></body></html>", 40);

    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "Title");
    CHECK(lines[1] == "First para.");
    CHECK(lines[2] == "one");
    CHECK(lines[3] == "two");
}

TEST_CASE("inline elements stay on the same line and whitespace collapses")
{
    auto lines = render_lines("<p>hello <b>brave</b>\n\n  <i>new</i> world</p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "hello brave new world");
}

TEST_CASE("br breaks a line inside a block")
{
    auto lines = render_lines("<p>line one<br>line two</p>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "line one");
    CHECK(lines[1] == "line two");
}

TEST_CASE("script, style and head content is not rendered")
{
    auto lines = render_lines("<html><head><title>nope</title>"
                              "<style>p { color: red }</style></head>"
                              "<body><script>var x = 1;</script><p>kept</p></body></html>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "kept");
}

TEST_CASE("text wraps at the requested screen width")
{
    const std::string html = "<p>the quick brown fox jumps over the lazy dog</p>";

    SUBCASE("32 columns")
    {
        auto lines = render_lines(html, 32);
        for (const auto &line : lines)
            CHECK(line.size() <= 32);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "the quick brown fox jumps over");
        CHECK(lines[1] == "the lazy dog");
    }

    SUBCASE("40 columns")
    {
        auto lines = render_lines(html, 40);
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == "the quick brown fox jumps over the lazy");
        CHECK(lines[1] == "dog");
    }

    SUBCASE("80 columns")
    {
        auto lines = render_lines(html, 80);
        REQUIRE(lines.size() == 1);
        CHECK(lines[0] == "the quick brown fox jumps over the lazy dog");
    }
}

TEST_CASE("a word longer than the screen is hard-broken")
{
    auto lines = render_lines("<p>aaaaaaaaaaaaaaaaaaaaaaaa b</p>", 16);

    // The overlong word fills a line, then what's left of it fits with "b".
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "aaaaaaaaaaaaaaaa");
    CHECK(lines[1] == "aaaaaaaa b");
}

TEST_CASE("empty blocks do not produce blank lines")
{
    auto lines = render_lines("<div><div><p></p><p>   </p><p>text</p></div></div>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "text");
}

TEST_CASE("character references are decoded")
{
    auto lines = render_lines("<p>fish &amp; chips &lt;3</p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "fish & chips <3");
}

TEST_CASE("malformed markup still renders")
{
    auto lines = render_lines("<p>unclosed<div>next", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "unclosed");
    CHECK(lines[1] == "next");
}

TEST_CASE("screen width is clamped and 0 keeps the current width")
{
    FNPretty renderer;
    CHECK(renderer.screenWidth() == 40); // default

    renderer.setScreenWidth(80);
    CHECK(renderer.screenWidth() == 80);

    renderer.setScreenWidth(0); // "no width given"
    CHECK(renderer.screenWidth() == 80);

    renderer.setScreenWidth(4); // unusably narrow
    CHECK(renderer.screenWidth() == 16);
}

TEST_CASE("rendered text is read out sequentially and then reports empty")
{
    FNPretty renderer;
    renderer.setLineEnding("\n");
    REQUIRE(renderer.renderDocument("<p>abcdef</p>"));

    const size_t total = renderer.available();
    REQUIRE(total == 7); // "abcdef\n"

    uint8_t buf[4];
    CHECK(renderer.readValue(buf, sizeof(buf)) == 4);
    CHECK(std::string((char *)buf, 4) == "abcd");
    CHECK(renderer.available() == total - 4);

    // Asking for more than is left returns only what remains.
    CHECK(renderer.readValue(buf, sizeof(buf)) == 3);
    CHECK(std::string((char *)buf, 3) == "ef\n");
    CHECK(renderer.available() == 0);
    CHECK(renderer.readValue(buf, sizeof(buf)) == 0);

    NetworkStatus ns;
    renderer.status(&ns);
    CHECK(ns.error == NDEV_STATUS::END_OF_FILE);
}
