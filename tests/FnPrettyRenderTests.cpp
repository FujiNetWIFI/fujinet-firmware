#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>

#include "fnpretty/fnpretty.h"

// Debug_printf() resolves to this; the firmware's implementation lives in
// utils.cpp with the rest of the system layer, which the renderer doesn't need.
void util_debug_printf(const char *fmt, ...) { (void)fmt; }

static std::vector<std::string> split_lines(const std::string &out)
{
    std::vector<std::string> lines;
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

// Small builder for tests that need flags/query/base URL beyond the plain
// width most tests only care about.
struct RenderOptions
{
    uint8_t width = 40;
    uint8_t flags = 0;
    std::string query;
    std::string baseUrl;
    PrettyCharset charset = PrettyCharset::Ascii;
};

// Applies opts to renderer (caller-owned, so tests can inspect it afterwards
// via linkCount()/linkUrl()), renders html and returns the split lines.
static std::vector<std::string> render_lines(FNPretty &renderer, const std::string &html, const RenderOptions &opts)
{
    // Render with LF line endings so expectations read normally here; the
    // Atari device sets 0x9B instead.
    renderer.setLineEnding("\n");
    renderer.setScreenWidth(opts.width);
    renderer.setFlags(opts.flags);
    renderer.setCharset(opts.charset);
    if (!opts.query.empty())
        renderer.setQuery(opts.query);
    if (!opts.baseUrl.empty())
        renderer.setBaseUrl(opts.baseUrl);
    REQUIRE(renderer.renderDocument(html));
    return split_lines(renderer.rendered());
}

// Convenience for the common case: just a screen width, no flags/query/base.
static std::vector<std::string> render_lines(const std::string &html, uint8_t width)
{
    FNPretty renderer;
    RenderOptions opts;
    opts.width = width;
    return render_lines(renderer, html, opts);
}

TEST_CASE("each block element renders on its own line, with margins between them")
{
    auto lines = render_lines("<html><body><h1>Title</h1><p>First para.</p>"
                              "<ul><li>one</li><li>two</li></ul></body></html>", 40);

    // h1 is a bordered, centered box (2 blank lines after, none before since
    // it starts the document); p gets a 1-line margin on both sides, merged
    // (not summed) with the h1's trailing margin and the ul's leading one;
    // consecutive li's get no margin between them.
    REQUIRE(lines.size() == 9);
    CHECK(lines[0] == "               +-------+");
    CHECK(lines[1] == "               | Title |");
    CHECK(lines[2] == "               +-------+");
    CHECK(lines[3] == "");
    CHECK(lines[4] == "");
    CHECK(lines[5] == "First para.");
    CHECK(lines[6] == "");
    CHECK(lines[7] == "  * one");
    CHECK(lines[8] == "  * two");
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

    // <div> auto-closes the open <p> (block inside inline content); the p's
    // trailing margin and the div's leading one merge into a single blank.
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "unclosed");
    CHECK(lines[1] == "");
    CHECK(lines[2] == "next");
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

    // Trailing line-ending runs are stripped at the end of a render, so
    // "abcdef" has no trailing "\n" here.
    const size_t total = renderer.available();
    REQUIRE(total == 6); // "abcdef"

    uint8_t buf[4];
    CHECK(renderer.readValue(buf, sizeof(buf)) == 4);
    CHECK(std::string((char *)buf, 4) == "abcd");
    CHECK(renderer.available() == total - 4);

    // Asking for more than is left returns only what remains.
    CHECK(renderer.readValue(buf, sizeof(buf)) == 2);
    CHECK(std::string((char *)buf, 2) == "ef");
    CHECK(renderer.available() == 0);
    CHECK(renderer.readValue(buf, sizeof(buf)) == 0);

    NetworkStatus ns;
    renderer.status(&ns);
    CHECK(ns.error == NDEV_STATUS::END_OF_FILE);
}

// ---------------------------------------------------------------------------
// Margins
// ---------------------------------------------------------------------------

TEST_CASE("blank line between paragraphs; margins collapse rather than sum")
{
    auto lines = render_lines("<p>one</p><p>two</p><h3>three</h3>", 40);

    // p/p: one blank line, not two. p/h3: h3's leading margin(1) merges with
    // p's trailing margin(1) into a single blank, not a sum of two.
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "one");
    CHECK(lines[1] == "");
    CHECK(lines[2] == "two");
    CHECK(lines[3] == "");
    CHECK(lines[4] == "three");
    CHECK(lines[5] == "=====");
}

// ---------------------------------------------------------------------------
// Headings
// ---------------------------------------------------------------------------

TEST_CASE("h1 renders as a bordered, centered box with nothing above it")
{
    auto lines = render_lines("<h1>Hi</h1><p>after</p>", 40);

    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "                 +----+");
    CHECK(lines[1] == "                 | Hi |");
    CHECK(lines[2] == "                 +----+");
    CHECK(lines[3] == "");
    CHECK(lines[4] == "");
    CHECK(lines[5] == "after");
}

TEST_CASE("h2 is centered with two blank lines around it")
{
    auto lines = render_lines("<p>before</p><h2>Mid</h2><p>after</p>", 40);

    REQUIRE(lines.size() == 7);
    CHECK(lines[0] == "before");
    CHECK(lines[1] == "");
    CHECK(lines[2] == "");
    CHECK(lines[3] == "                  Mid");
    CHECK(lines[4] == "");
    CHECK(lines[5] == "");
    CHECK(lines[6] == "after");
}

TEST_CASE("h3 underlines with = matching the text length")
{
    auto lines = render_lines("<h3>Section</h3>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Section");
    CHECK(lines[1] == "=======");
}

TEST_CASE("h5 underlines with - matching the text length")
{
    auto lines = render_lines("<h5>Sub</h5>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Sub");
    CHECK(lines[1] == "---");
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

TEST_CASE("ul renders a bullet with wrapped continuation aligned under the text")
{
    auto lines = render_lines(
        "<ul><li>a very long item that will definitely need to wrap onto a second line for sure</li></ul>", 40);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "  * a very long item that will");
    CHECK(lines[1] == "    definitely need to wrap onto a");
    CHECK(lines[2] == "    second line for sure");
}

TEST_CASE("ol right-aligns markers to the widest number in the list")
{
    std::string html = "<ol>";
    for (int i = 0; i < 12; i++)
        html += "<li>x</li>";
    html += "</ol>";

    auto lines = render_lines(html, 40);

    REQUIRE(lines.size() == 12);
    CHECK(lines[0] == "   1. x");
    CHECK(lines[8] == "   9. x");
    CHECK(lines[9] == "  10. x");
    CHECK(lines[11] == "  12. x");
}

TEST_CASE("ol start attribute is honoured")
{
    auto lines = render_lines("<ol start=\"5\"><li>a</li><li>b</li></ol>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  5. a");
    CHECK(lines[1] == "  6. b");
}

TEST_CASE("a nested list indents under its parent item's text with no extra blank line")
{
    auto lines = render_lines("<ul><li>outer<ul><li>inner</li></ul></li></ul>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  * outer");
    CHECK(lines[1] == "      * inner");
}

// ---------------------------------------------------------------------------
// Other block elements
// ---------------------------------------------------------------------------

TEST_CASE("blockquote indents its contents by 2")
{
    auto lines = render_lines("<blockquote>quoted text</blockquote>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "  quoted text");
}

TEST_CASE("hr emits a full-width rule")
{
    auto lines = render_lines("<p>a</p><hr><p>b</p>", 20);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "a");
    CHECK(lines[1] == "");
    CHECK(lines[2] == std::string(20, '-'));
    CHECK(lines[3] == "");
    CHECK(lines[4] == "b");
}

TEST_CASE("pre preserves internal line breaks")
{
    auto lines = render_lines("<pre>line one\nline two\n  indented</pre>", 40);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "line one");
    CHECK(lines[1] == "line two");
    CHECK(lines[2] == "  indented");
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

TEST_CASE("table renders a full grid with outer borders at 80 columns")
{
    auto lines = render_lines(
        "<table><tr><th>Name</th><th>Qty</th></tr><tr><td>Widget</td><td>12</td></tr></table>", 80);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "+--------+-----+");
    CHECK(lines[1] == "| Name   | Qty |");
    CHECK(lines[2] == "+--------+-----+");
    CHECK(lines[3] == "| Widget | 12  |");
    CHECK(lines[4] == "+--------+-----+");
}

TEST_CASE("a narrow table keeps its borders and collapses the cell padding")
{
    auto lines = render_lines(
        "<table><tr><th>Name</th><th>Qty</th></tr><tr><td>Widget</td><td>12</td></tr></table>", 40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "+------+---+");
    CHECK(lines[1] == "|Name  |Qty|");
    CHECK(lines[2] == "+------+---+");
    CHECK(lines[3] == "|Widget|12 |");
    CHECK(lines[4] == "+------+---+");

    // Every row is the same length and framed at both ends, so the grid is
    // rectangular at any width - only the padding inside the cells is gone.
    for (const auto &line : lines)
    {
        CHECK((line.front() == '+' || line.front() == '|'));
        CHECK((line.back() == '+' || line.back() == '|'));
        CHECK(line.size() == lines[0].size());
    }
}

TEST_CASE("table shrinks columns and wraps cells to multiple lines at a narrow width")
{
    auto lines = render_lines(
        "<table><tr><th>Name</th><th>Description</th></tr>"
        "<tr><td>Widget</td><td>a rather long description that needs wrapping</td></tr></table>",
        24);

    // Column widths are [2, 19]: collapsing the cell padding leaves more for
    // content than the padded form would at this width.
    REQUIRE(lines.size() == 8);
    CHECK(lines[0] == "+--+-------------------+");
    CHECK(lines[1] == "|Na|Description        |");
    CHECK(lines[2] == "|me|                   |");
    CHECK(lines[3] == "+--+-------------------+");
    CHECK(lines[4] == "|Wi|a rather long      |");
    CHECK(lines[5] == "|dg|description that   |");
    CHECK(lines[6] == "|et|needs wrapping     |");
    CHECK(lines[7] == "+--+-------------------+");

    for (const auto &line : lines)
        CHECK(line.size() == 24);
}

TEST_CASE("a narrow table's junctions line up with its column borders")
{
    auto lines = render_lines(
        "<table><tr><th>A</th><th>BB</th><th>CCC</th></tr>"
        "<tr><td>1</td><td>22</td><td>333</td></tr></table>",
        40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "+-+--+---+");
    CHECK(lines[1] == "|A|BB|CCC|");
    CHECK(lines[2] == "+-+--+---+");
    CHECK(lines[3] == "|1|22|333|");
    CHECK(lines[4] == "+-+--+---+");

    // Every rule begins and ends with a corner, and its interior junctions
    // fall on the same columns as the data rows' borders.
    for (size_t i : {0u, 2u, 4u})
    {
        const std::string &sep = lines[i];
        CHECK(sep.front() == '+');
        CHECK(sep.back() == '+');
        for (size_t pos = 0; pos < sep.size(); pos++)
            CHECK((sep[pos] == '+') == (lines[1][pos] == '|'));
    }
}

TEST_CASE("table column borders stay vertically aligned across every row")
{
    auto lines = render_lines(
        "<table><tr><th>A</th><th>BB</th><th>CCC</th></tr>"
        "<tr><td>1</td><td>22</td><td>333</td></tr>"
        "<tr><td>x</td><td>yy</td><td>zzz</td></tr></table>",
        40);

    REQUIRE(lines.size() == 7);

    // Collect the "|" offsets of every non-separator (data/header) row and
    // require them to be identical, regardless of each row's own content.
    std::vector<size_t> firstPipeOffsets;
    for (const auto &line : lines)
    {
        if (line.find('|') == std::string::npos)
            continue; // separator row - contains '+', not '|'

        std::vector<size_t> offsets;
        for (size_t pos = line.find('|'); pos != std::string::npos; pos = line.find('|', pos + 1))
            offsets.push_back(pos);

        if (firstPipeOffsets.empty())
            firstPipeOffsets = offsets;
        else
            CHECK(offsets == firstPipeOffsets);
    }
    CHECK(firstPipeOffsets.size() == 4); // two outer borders plus two junctions
}

TEST_CASE("collapsing the cell padding hands the reclaimed columns to content")
{
    // A single, unbreakably long line of text forces the column to shrink to
    // exactly the available width, so the rendered column width is a direct
    // readout of how much space the layout math handed to content.
    const std::string html =
        "<table><tr><td>this row holds a very long single line of unbroken text used only to "
        "force the column to shrink to the available width for measurement purposes</td></tr>"
        "<tr><td>x</td></tr></table>";
    const uint8_t width = 40;

    auto lines = render_lines(html, width);
    REQUIRE(!lines.empty());

    // The rule is the frame plus the content column, so its dash run is a
    // direct readout of the width the layout math handed to content.
    const std::string &rule = lines[0];
    CHECK(rule.front() == '+');
    CHECK(rule.back() == '+');
    size_t measuredWidth = rule.size() - 2;
    CHECK(rule.substr(1, measuredWidth).find_first_not_of('-') == std::string::npos);

    // A padded table (kCellPaddingMinWidth is 80) reserves 3*ncols+1 = 4
    // columns of frame for a single column, versus ncols+1 = 2 once the
    // padding is collapsed - so the content column is exactly 2 wider here.
    size_t paddedOverhead = 3 * 1 + 1;
    size_t hypotheticalPaddedWidth = width - paddedOverhead;
    CHECK(measuredWidth == hypotheticalPaddedWidth + 2);
}

TEST_CASE("a single-column narrow table is framed with no inner padding")
{
    // Two rows (cellCount == 2) keep this classified as a data table rather
    // than a one-cell layout wrapper (see isLayoutTable()).
    auto lines = render_lines("<table><tr><td>Alpha</td></tr><tr><td>Beta</td></tr></table>", 40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "+-----+");
    CHECK(lines[1] == "|Alpha|");
    CHECK(lines[2] == "+-----+");
    CHECK(lines[3] == "|Beta |");
    CHECK(lines[4] == "+-----+");

    // Content starts immediately after the border, with no padding column.
    CHECK(lines[1][1] == 'A');
}

// ---------------------------------------------------------------------------
// Layout tables vs. data tables (isLayoutTable())
// ---------------------------------------------------------------------------

TEST_CASE("a single-cell wrapper table renders its contents as plain blocks, not a grid")
{
    auto lines = render_lines(
        "<table><tr><td><h3>Title</h3><p>Body text</p></td></tr></table>", 40);

    // One cell total -> layout table: no grid at all, just the heading and
    // paragraph rendered the same as if the <table>/<tr>/<td> weren't there.
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "Title");
    CHECK(lines[1] == "=====");
    CHECK(lines[2] == "");
    CHECK(lines[3] == "Body text");

    for (const auto &line : lines)
    {
        CHECK(line.find('|') == std::string::npos);
        CHECK(line.find('+') == std::string::npos);
    }
}

TEST_CASE("a plain multi-cell table with no nested tables still renders as a grid")
{
    auto lines = render_lines(
        "<table><tr><td>A</td><td>B</td></tr><tr><td>C</td><td>D</td></tr></table>", 40);

    // The grid keeps its frame; at narrow widths the only difference from wider
    // tables is that cells have no padding inside the frame.
    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "+-+-+");
    CHECK(lines[1] == "|A|B|");
    CHECK(lines[2] == "+-+-+");
    CHECK(lines[3] == "|C|D|");
    CHECK(lines[4] == "+-+-+");
}

TEST_CASE("a single-cell layout table wrapping a genuine data table renders only the inner grid")
{
    auto lines = render_lines(
        "<table><tr><td>outer"
        "<table><tr><td>A</td><td>B</td></tr><tr><td>C</td><td>D</td></tr></table>"
        "</td></tr></table>",
        40);

    // The outer table has one cell -> layout, rendered transparently; the
    // inner table has four -> data, rendered as its own grid. The outer
    // table contributes no borders of its own. The inner grid keeps its frame
    // with collapsed inner padding.
    REQUIRE(lines.size() == 7);
    CHECK(lines[0] == "outer");
    CHECK(lines[1] == "");
    CHECK(lines[2] == "+-+-+");
    CHECK(lines[3] == "|A|B|");
    CHECK(lines[4] == "+-+-+");
    CHECK(lines[5] == "|C|D|");
    CHECK(lines[6] == "+-+-+");
}

TEST_CASE("a data table whose cell contains a nested table renders linearly, not as a grid")
{
    auto lines = render_lines(
        "<table><tr><td>left<table><tr><td>nested</td></tr></table></td><td>right</td></tr></table>",
        40);

    // One of the two top-level cells contains a descendant <table>, so the
    // whole table degrades to a layout table (see isLayoutTable()): no grid,
    // but "left" and the nested table's "nested" must still land on separate
    // lines rather than running together as "leftnested".
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "left");
    CHECK(lines[1] == "nested");
    CHECK(lines[2] == "right");

    for (const auto &line : lines)
    {
        CHECK(line.find('|') == std::string::npos);
        CHECK(line.find('+') == std::string::npos);
    }
}

TEST_CASE("deeply nested layout tables terminate and render sane linear output")
{
    auto lines = render_lines(
        "<table><tr><td>L1"
        "<table><tr><td>L2"
        "<table><tr><td>L3"
        "<table><tr><td>L4"
        "<table><tr><td>L5</td></tr></table>"
        "</td></tr></table>"
        "</td></tr></table>"
        "</td></tr></table>"
        "</td></tr></table>",
        40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "L1");
    CHECK(lines[1] == "L2");
    CHECK(lines[2] == "L3");
    CHECK(lines[3] == "L4");
    CHECK(lines[4] == "L5");
}

// ---------------------------------------------------------------------------
// Query / section selection
// ---------------------------------------------------------------------------

TEST_CASE("a query selects only the matching section")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.query = "#b";
    auto lines = render_lines(renderer, "<div id=\"a\"><p>A content</p></div><div id=\"b\"><p>B content</p></div>", opts);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "B content");
}

TEST_CASE("a query matching nothing yields empty output")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.query = ".nope";
    auto lines = render_lines(renderer, "<div id=\"a\"><p>A content</p></div>", opts);

    CHECK(lines.empty());
    CHECK(renderer.available() == 0);
}

TEST_CASE("nested duplicate query matches are not rendered twice")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.query = "div";
    auto lines = render_lines(renderer, "<div><div><p>inner</p></div></div>", opts);

    // Both the outer and inner <div> match "div"; only the outer (first
    // accepted) is rendered, so "inner" appears exactly once.
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "inner");
}

TEST_CASE("link numbering is document-global even when a query selects a later section")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.flags = PRETTY_RENDER_LINKS;
    opts.query = "#sec2";
    auto lines = render_lines(renderer,
        "<a href=\"/one\">one</a><div id=\"sec2\"><a href=\"/two\">two</a></div>", opts);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "<two>[2]");
    CHECK(renderer.linkCount() == 2);
    CHECK(renderer.linkUrl(1) == "/one");
    CHECK(renderer.linkUrl(2) == "/two");
}

// ---------------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------------

TEST_CASE("links are off by default: no markers and no collected links")
{
    FNPretty renderer;
    RenderOptions opts;
    auto lines = render_lines(renderer, "<p>See <a href=\"/more\">more</a>.</p>", opts);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "See more.");
    CHECK(renderer.linkCount() == 0);
}

TEST_CASE("links on: numbered in document order, deduplicated, fragments skipped")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.flags = PRETTY_RENDER_LINKS;
    opts.baseUrl = "https://example.com/dir/page.html";
    auto lines = render_lines(renderer,
        "<p>See <a href=\"/more\">more</a> and <a href=\"/more\">again</a> and <a href=\"#top\">top</a>.</p>", opts);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "See <more>[1] and <again>[1] and top.");

    CHECK(renderer.linkCount() == 1);
    CHECK(renderer.linkUrl(1) == "https://example.com/more");
    CHECK(renderer.linkUrl(0) == ""); // out of range (0-indexed is invalid)
    CHECK(renderer.linkUrl(2) == ""); // out of range (only 1 link collected)
}

TEST_CASE("relative href resolution against a base URL")
{
    auto resolve = [](const std::string &base, const std::string &href) {
        FNPretty renderer;
        RenderOptions opts;
        opts.flags = PRETTY_RENDER_LINKS;
        opts.baseUrl = base;
        render_lines(renderer, "<a href=\"" + href + "\">x</a>", opts);
        return renderer.linkUrl(1);
    };

    const std::string base = "https://example.com/dir/page.html";
    CHECK(resolve(base, "about") == "https://example.com/dir/about");
    CHECK(resolve(base, "/about") == "https://example.com/about");
    CHECK(resolve(base, "../up") == "https://example.com/up");
    CHECK(resolve(base, "//cdn.example.com/x") == "https://cdn.example.com/x");
    CHECK(resolve(base, "https://other/x") == "https://other/x");
}

TEST_CASE("an in-document <base href> overrides the base URL")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.flags = PRETTY_RENDER_LINKS;
    opts.baseUrl = "https://example.com/dir/page.html";
    render_lines(renderer, "<base href=\"/root/\"><a href=\"x\">x</a>", opts);

    CHECK(renderer.linkUrl(1) == "https://example.com/root/x");
}

// ---------------------------------------------------------------------------
// Deliverable sample: rendered twice (links off, links on) at width 40, for
// eyeballing - see the task report for the pasted output.
// ---------------------------------------------------------------------------

TEST_CASE("deliverable sample renders and both link modes agree except for markers")
{
    const std::string html =
        "<h1>Fuji News</h1><h3>Today</h3><p>A short paragraph that will need to wrap at forty columns.</p>"
        "<ul><li>first item</li><li>second item that is long enough to wrap onto another line</li>"
        "<li>nested:<ul><li>inner</li></ul></li></ul>"
        "<table><tr><th>Name</th><th>Qty</th></tr><tr><td>Widget</td><td>12</td></tr></table>"
        "<p>See <a href=\"/more\">more news</a> and <a href=\"#top\">top</a>.</p>";

    auto linksOff = render_lines(html, 40);
    REQUIRE(linksOff.back() == "See more news and top.");

    FNPretty renderer;
    RenderOptions opts;
    opts.width = 40;
    opts.flags = PRETTY_RENDER_LINKS;
    opts.baseUrl = "https://fujinet.online/news/";
    auto linksOn = render_lines(renderer, html, opts);
    REQUIRE(linksOn.back() == "See <more news>[1] and top.");

    // Everything above the final paragraph is identical either way.
    REQUIRE(linksOff.size() == linksOn.size());
    for (size_t i = 0; i + 1 < linksOff.size(); i++)
        CHECK(linksOff[i] == linksOn[i]);

    // "/more" is an absolute path, so it replaces the base's whole path
    // (including "/news/"), per RFC 3986 5.3 case 5.
    CHECK(renderer.linkCount() == 1);
    CHECK(renderer.linkUrl(1) == "https://fujinet.online/more");
}

// ---------------------------------------------------------------------------
// Block boundaries inside a captured subtree
// ---------------------------------------------------------------------------

TEST_CASE("a heading inside a table cell is separated from following text")
{
    auto lines = render_lines(
        "<table><tr><td><h3>Head</h3>body</td><td>b</td></tr></table>", 40);

    // A heading inside a cell is separated from following text by a block
    // boundary (space in the cell's text); at narrow widths the frame is kept
    // with collapsed inner padding.
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "+---------+-+");
    CHECK(lines[1] == "|Head body|b|");
    CHECK(lines[2] == "+---------+-+");
}

TEST_CASE("a nested table's text is separated from its parent cell's text")
{
    auto lines = render_lines(
        "<table><tr><td>outer<table><tr><td>inner</td></tr></table></td></tr></table>", 40);

    // Both tables have a single cell, so both are layout tables (see
    // isLayoutTable()): no grid at any level, just each td as its own block
    // in document order - "outer" and "inner" land on separate lines rather
    // than running together.
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "outer");
    CHECK(lines[1] == "inner");
}

TEST_CASE("block children inside link text are separated")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.flags = PRETTY_RENDER_LINKS;
    auto lines = render_lines(renderer,
        "<a href=\"/x\"><div>one</div><div>two</div></a>", opts);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "<one two>[1]");
}

// ---------------------------------------------------------------------------
// Indent clamping on narrow screens
// ---------------------------------------------------------------------------

TEST_CASE("deeply nested lists on a narrow screen saturate the indent instead of collapsing to one column")
{
    std::string html = "<ul>";
    for (int i = 0; i < 5; i++)
        html += "<li>level " + std::to_string(i) + "<ul>";
    html += "<li>deep text</li>";
    for (int i = 0; i < 5; i++)
        html += "</ul></li>";
    html += "</ul>";

    auto lines = render_lines(html, 16);

    REQUIRE(!lines.empty());
    for (const auto &line : lines)
        CHECK(line.size() <= 16);

    // The innermost item's text must still come out in multi-character
    // chunks, not one letter per line.
    bool foundMultiCharChunk = false;
    for (const auto &line : lines)
    {
        size_t star = line.find('*');
        std::string text = star == std::string::npos ? line : line.substr(star + 2);
        if (text.find("deep") != std::string::npos || text.find("text") != std::string::npos)
            foundMultiCharChunk = true;
    }
    CHECK(foundMultiCharChunk);

    // The marker column stops growing once the indent saturates: the last
    // two list levels ("level 4" and "deep text") must land at the same
    // marker column rather than continuing to step inward.
    auto markerCol = [](const std::string &line) { return line.find('*'); };
    size_t lastLevelCol = std::string::npos, deepTextCol = std::string::npos;
    for (const auto &line : lines)
    {
        if (line.find("level 4") != std::string::npos)
            lastLevelCol = markerCol(line);
        if (line.find("deep") != std::string::npos)
            deepTextCol = markerCol(line);
    }
    REQUIRE(lastLevelCol != std::string::npos);
    REQUIRE(deepTextCol != std::string::npos);
    CHECK(lastLevelCol == deepTextCol);
}

TEST_CASE("normal-width nested list layout is unchanged")
{
    auto lines = render_lines("<ul><li>a<ul><li>b</li></ul></li></ul>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  * a");
    CHECK(lines[1] == "      * b");
}

// ---------------------------------------------------------------------------
// ASCII folding (fn_utf8_to_ascii)
// ---------------------------------------------------------------------------

static bool is_all_ascii(const std::string &s)
{
    for (unsigned char c : s)
    {
        if (c >= 0x80)
            return false;
    }
    return true;
}

TEST_CASE("transliteration folds decoded entities to printable ASCII")
{
    auto lines = render_lines("<p>caf&eacute; &mdash; 5 &times; 3 &hellip;</p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "cafe -- 5 x 3 ...");

    for (const auto &line : lines)
        CHECK(is_all_ascii(line));
}

TEST_CASE("table column alignment survives multi-byte cell content")
{
    // Regression guard: before ASCII folding, a cell containing the (3-byte
    // UTF-8) en dash counted more bytes than it occupies display columns, so
    // the table's column-width math and a plain-ASCII row's padding
    // disagreed and the grid's "|" borders no longer lined up. Folding to
    // ASCII first means every byte in a cell is exactly one display column,
    // so the grid stays rectangular. At 40 columns the cell padding is
    // collapsed but the frame remains, so every row is the same length.
    auto lines = render_lines(
        "<table><tr><td>1979&ndash;1984</td><td>ok</td></tr>"
        "<tr><td>x</td><td>y</td></tr></table>",
        40);

    REQUIRE(lines.size() == 5);
    const std::string rule = "+---------+--+";
    CHECK(lines[0] == rule);
    CHECK(lines[1] == "|1979-1984|ok|");
    CHECK(lines[2] == rule);
    CHECK(lines[3] == "|x        |y |");
    CHECK(lines[4] == rule);
}

TEST_CASE("entities are not double-decoded")
{
    // The source's "&amp;lt;" means the reader should see the literal text
    // "&lt;" - Gumbo decodes "&amp;" to "&" exactly once, and appendInline()
    // must not run fn_decode_entities() on top of that (it would wrongly
    // decode the resulting "&lt;" a second time into "<").
    auto lines = render_lines("<p>&amp;lt;tag&amp;gt;</p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "&lt;tag&gt;");
}

TEST_CASE("pre keeps its line breaks while still being folded to ASCII")
{
    auto lines = render_lines("<pre>a&mdash;b\nc</pre>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "a--b");
    CHECK(lines[1] == "c");
}

TEST_CASE("wrapping counts folded characters, not raw UTF-8 bytes")
{
    // Eight "café" words (folding each to the 4-byte/4-column "cafe") joined
    // by single spaces. At width 20 this wraps to exactly two lines of
    // "cafe cafe cafe cafe" (19 bytes each) - if folding happened after
    // wrapping (or not at all), the multi-byte é would inflate the byte
    // count relative to the display column count.
    auto lines = render_lines(
        "<p>caf&eacute; caf&eacute; caf&eacute; caf&eacute; "
        "caf&eacute; caf&eacute; caf&eacute; caf&eacute;</p>",
        20);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "cafe cafe cafe cafe");
    CHECK(lines[1] == "cafe cafe cafe cafe");

    for (const auto &line : lines)
    {
        CHECK(is_all_ascii(line));
        CHECK(line.size() <= 20);
    }
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

TEST_CASE("img with alt text renders a bracketed placeholder")
{
    auto lines = render_lines("<p><img alt=\"Atari Logo\"></p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "[IMG Atari Logo]");
}

TEST_CASE("img with no alt, or an empty alt, renders a bare placeholder")
{
    auto noAlt = render_lines("<p><img></p>", 40);
    REQUIRE(noAlt.size() == 1);
    CHECK(noAlt[0] == "[IMG]");

    auto emptyAlt = render_lines("<p><img alt=\"\"></p>", 40);
    REQUIRE(emptyAlt.size() == 1);
    CHECK(emptyAlt[0] == "[IMG]");
}

TEST_CASE("img alt text has internal whitespace collapsed and trimmed")
{
    auto lines = render_lines("<p><img alt=\"  spaced   out  \"></p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "[IMG spaced out]");
}

TEST_CASE("an image-only link renders its placeholder as the link text, with the index now visible")
{
    FNPretty renderer;
    RenderOptions opts;
    opts.flags = PRETTY_RENDER_LINKS;
    opts.baseUrl = "https://example.com/dir/page.html";
    auto lines = render_lines(renderer, "<a href=\"/logo\"><img alt=\"Logo\"></a>", opts);

    // Previously an image-only anchor captured empty text and the marker was
    // suppressed entirely, even though the link still consumed a number -
    // an "invisible" gap in the visible indices. Now the placeholder is
    // captured as the link's text, so the index is visible like any other
    // link.
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "<[IMG Logo]>[1]");
    CHECK(renderer.linkCount() == 1);
    CHECK(renderer.linkUrl(1) == "https://example.com/logo");
}

TEST_CASE("an image inside a table cell appears in that cell's text")
{
    auto lines = render_lines(
        "<table><tr><td><img alt=\"pic\"></td><td>b</td></tr></table>", 40);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "+---------+-+");
    CHECK(lines[1] == "|[IMG pic]|b|");
    CHECK(lines[2] == "+---------+-+");
}

TEST_CASE("img placeholder spacing is faithful to the surrounding markup")
{
    auto spaced = render_lines("<p>Hello <img alt=\"x\"> there</p>", 40);
    REQUIRE(spaced.size() == 1);
    CHECK(spaced[0] == "Hello [IMG x] there");

    auto tight = render_lines("<p>Hello<img alt=\"x\"></p>", 40);
    REQUIRE(tight.size() == 1);
    CHECK(tight[0] == "Hello[IMG x]");
}

TEST_CASE("img alt text is ASCII-folded like other text")
{
    auto lines = render_lines("<p><img alt=\"caf&eacute;\"></p>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "[IMG cafe]");
}

// ---------------------------------------------------------------------------
// setOption() - "!key=value" query payload
// ---------------------------------------------------------------------------

TEST_CASE("setOption: links=1/0 toggles renderLinks")
{
    FNPretty renderer;
    CHECK(renderer.renderLinks() == false);

    CHECK(renderer.setOption("!links=1") == PrettyOption::Applied);
    CHECK(renderer.renderLinks() == true);

    CHECK(renderer.setOption("!links=0") == PrettyOption::Applied);
    CHECK(renderer.renderLinks() == false);
}

TEST_CASE("setOption: width sets screenWidth; 0 leaves it unchanged")
{
    FNPretty renderer;
    CHECK(renderer.setOption("!width=80") == PrettyOption::Applied);
    CHECK(renderer.screenWidth() == 80);

    CHECK(renderer.setOption("!width=0") == PrettyOption::Applied);
    CHECK(renderer.screenWidth() == 80);
}

TEST_CASE("setOption: eol changes the line ending used by a following rerender; 0 is invalid")
{
    FNPretty renderer;
    renderer.setLineEnding("\n");
    REQUIRE(renderer.renderDocument("<p>a</p><p>b</p>"));
    CHECK(renderer.rendered().find('\r') == std::string::npos);

    CHECK(renderer.setOption("!eol=13") == PrettyOption::Applied);
    CHECK(renderer.rerender());
    CHECK(renderer.rendered().find('\r') != std::string::npos);

    CHECK(renderer.setOption("!eol=0") == PrettyOption::Invalid);
}

TEST_CASE("setOption: keys are matched case-insensitively")
{
    FNPretty renderer;
    CHECK(renderer.setOption("!LINKS=1") == PrettyOption::Applied);
    CHECK(renderer.renderLinks() == true);

    CHECK(renderer.setOption("!Width=32") == PrettyOption::Applied);
    CHECK(renderer.screenWidth() == 32);
}

TEST_CASE("setOption: whitespace around key and value is tolerated")
{
    FNPretty renderer;
    CHECK(renderer.setOption("! links = 1 ") == PrettyOption::Applied);
    CHECK(renderer.renderLinks() == true);
}

TEST_CASE("setOption: invalid forms")
{
    FNPretty renderer;
    CHECK(renderer.setOption("!bogus=1") == PrettyOption::Invalid);   // unknown key
    CHECK(renderer.setOption("!links") == PrettyOption::Invalid);     // no '='
    CHECK(renderer.setOption("!links=") == PrettyOption::Invalid);    // empty value
    CHECK(renderer.setOption("!width=abc") == PrettyOption::Invalid); // non-numeric value
    CHECK(renderer.setOption("!=1") == PrettyOption::Invalid);        // empty key
}

TEST_CASE("setOption: NotAnOption for selectors and link indices, which fall through unchanged")
{
    FNPretty renderer;
    CHECK(renderer.setOption("") == PrettyOption::NotAnOption);
    CHECK(renderer.setOption("div.content") == PrettyOption::NotAnOption);
    CHECK(renderer.setOption("#main") == PrettyOption::NotAnOption);
    CHECK(renderer.setOption("42") == PrettyOption::NotAnOption);
}

TEST_CASE("setOption: !links=1 followed by rerender makes [n] markers appear")
{
    FNPretty renderer;
    renderer.setLineEnding("\n");
    REQUIRE(renderer.renderDocument("<p>See <a href=\"/more\">more</a>.</p>"));
    CHECK(renderer.rendered().find('[') == std::string::npos);

    CHECK(renderer.setOption("!links=1") == PrettyOption::Applied);
    CHECK(renderer.rerender());
    CHECK(renderer.rendered().find("[1]") != std::string::npos);
}


// ============================================================================
// Native (ATASCII) graphics
//
// Expectations are written as raw byte values rather than escapes in string
// literals: "\x12A" is a single (overflowing) hex escape in C++, not "\x12"
// followed by 'A', so every glyph goes through gl()/rep() instead.
// ============================================================================

// One glyph, as a string that can be concatenated safely.
static std::string gl(unsigned char c)
{
    return std::string(1, (char)c);
}

// n copies of one glyph.
static std::string rep(unsigned char c, size_t n)
{
    return std::string(n, (char)c);
}

// The renderer's inverse-video transform, spelled out independently here so a
// bug in FNPretty::inverseText() cannot make its own expectations agree with
// it. The '}' / '~' fold is deliberately NOT reproduced - the one test that
// exercises it writes the folded text out by hand.
static std::string inv(const std::string &s)
{
    std::string out;
    for (char c : s)
        out += (char)((unsigned char)c | 0x80);
    return out;
}

// Codes the Atari screen editor acts on instead of printing.
static bool is_e_control(unsigned char c)
{
    return c == 0x1B || (c >= 0x1C && c <= 0x1F) || c == 0x7D || c == 0x7E ||
           c == 0x7F || c == 0x9B || (c >= 0x9C && c <= 0x9F) || c >= 0xFD;
}

static std::vector<std::string> render_atascii(const std::string &html, uint8_t width)
{
    FNPretty renderer;
    RenderOptions opts;
    opts.width = width;
    opts.charset = PrettyCharset::Atascii;
    return render_lines(renderer, html, opts);
}

TEST_CASE("ATASCII h1 is a solid inverse block")
{
    auto lines = render_atascii("<h1>Title</h1>", 40);

    // Same geometry as the ASCII h1 (boxInner 7, boxWidth 9, centered at
    // column 15) - only the frame is gone, replaced by inverse-video fill.
    REQUIRE(lines.size() == 3);
    std::string pad(15, ' ');
    CHECK(lines[0] == pad + inv(std::string(9, ' ')));
    CHECK(lines[1] == pad + inv("  Title  "));
    CHECK(lines[2] == pad + inv(std::string(9, ' ')));
}

TEST_CASE("a wrapped ATASCII h1 fills the block out to the box width")
{
    // 36 columns of text is exactly the wrap width at 40 columns, so the box
    // grows to the full screen and every row must still be boxWidth long.
    auto lines = render_atascii("<h1>A rather long heading that must wrap</h1>", 40);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == inv(std::string(40, ' ')));
    // boxInner is 38, the text is 36, so the 2 columns of slack split evenly
    // and each side then gets the extra border column: 2 + 36 + 2.
    CHECK(lines[1] == inv("  A rather long heading that must wrap  "));
    CHECK(lines[2] == inv(std::string(40, ' ')));
    CHECK(lines[1].size() == 40);
}

TEST_CASE("ATASCII h2 is an outlined box")
{
    auto lines = render_atascii("<h2>Title</h2>", 40);

    REQUIRE(lines.size() == 3);
    std::string pad(15, ' ');
    CHECK(lines[0] == pad + gl(0x11) + rep(0x12, 7) + gl(0x05));
    CHECK(lines[1] == pad + gl(0x7C) + " Title " + gl(0x7C));
    CHECK(lines[2] == pad + gl(0x1A) + rep(0x12, 7) + gl(0x03));
}

TEST_CASE("ATASCII h3 is inverse video; h4-h6 keep an underline, in box glyphs")
{
    auto lines = render_atascii("<h3>Three</h3><h4>Four</h4>"
                                "<h5>Five</h5><h6>Six</h6>", 40);

    REQUIRE(lines.size() == 10);
    CHECK(lines[0] == inv("Three")); // no underline row of its own
    CHECK(lines[1] == "");
    CHECK(lines[2] == "Four");
    CHECK(lines[3] == rep(0x12, 4));
    CHECK(lines[4] == "");
    CHECK(lines[5] == "Five");
    CHECK(lines[6] == rep(0x0E, 4));
    CHECK(lines[7] == "");
    CHECK(lines[8] == "Six");
    CHECK(lines[9] == rep(0x0E, 3));
}

TEST_CASE("ATASCII unordered lists use the ball glyph")
{
    auto lines = render_atascii("<ul><li>one</li><li>two</li></ul>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  " + gl(0x14) + " one");
    CHECK(lines[1] == "  " + gl(0x14) + " two");
}

TEST_CASE("ATASCII ordered lists keep their ASCII numbering")
{
    // Only the furniture is platform-specific; digits are digits.
    auto lines = render_atascii("<ol><li>one</li><li>two</li></ol>", 40);

    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "  1. one");
    CHECK(lines[1] == "  2. two");
}

TEST_CASE("a padded ATASCII table draws a real grid")
{
    // 80 columns is at kCellPaddingMinWidth, so cells keep their padding.
    auto lines = render_atascii("<table><tr><th>A</th><th>B</th></tr>"
                                "<tr><td>1</td><td>2</td></tr></table>", 80);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == gl(0x11) + rep(0x12, 3) + gl(0x17) + rep(0x12, 3) + gl(0x05));
    CHECK(lines[1] == gl(0x7C) + " A " + gl(0x7C) + " B " + gl(0x7C));
    CHECK(lines[2] == gl(0x01) + rep(0x12, 3) + gl(0x13) + rep(0x12, 3) + gl(0x04));
    CHECK(lines[3] == gl(0x7C) + " 1 " + gl(0x7C) + " 2 " + gl(0x7C));
    CHECK(lines[4] == gl(0x1A) + rep(0x12, 3) + gl(0x18) + rep(0x12, 3) + gl(0x03));
}

TEST_CASE("a narrow ATASCII table keeps its box frame with no inner padding")
{
    auto lines = render_atascii("<table><tr><th>A</th><th>B</th></tr>"
                                "<tr><td>1</td><td>2</td></tr></table>", 40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == gl(0x11) + rep(0x12, 1) + gl(0x17) + rep(0x12, 1) + gl(0x05));
    CHECK(lines[1] == gl(0x7C) + "A" + gl(0x7C) + "B" + gl(0x7C));
    CHECK(lines[2] == gl(0x01) + rep(0x12, 1) + gl(0x13) + rep(0x12, 1) + gl(0x04));
    CHECK(lines[3] == gl(0x7C) + "1" + gl(0x7C) + "2" + gl(0x7C));
    CHECK(lines[4] == gl(0x1A) + rep(0x12, 1) + gl(0x18) + rep(0x12, 1) + gl(0x03));
}

TEST_CASE("ATASCII hr is a run of the box-drawing horizontal")
{
    auto lines = render_atascii("<p>x</p><hr><p>y</p>", 40);

    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "x");
    CHECK(lines[1] == "");
    CHECK(lines[2] == rep(0x12, 40));
    CHECK(lines[3] == "");
    CHECK(lines[4] == "y");
}

TEST_CASE("inverse video folds the two codes the screen editor would eat")
{
    // Inverting '}' and '~' would give 0xFD (bell) and 0xFE (delete char);
    // the delete would swallow a column and skew everything drawn around it.
    auto lines = render_atascii("<h3>a}b~c</h3>", 40);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == inv("a]b-c"));
    CHECK(lines[0].find((char)0xFD) == std::string::npos);
    CHECK(lines[0].find((char)0xFE) == std::string::npos);
}

TEST_CASE("nothing ATASCII renders is a code the screen editor acts on")
{
    auto lines = render_atascii("<h1>Head</h1><h2>Sub</h2><h3>Three</h3>"
                                "<h4>Four</h4><ul><li>item</li></ul><hr>"
                                "<table><tr><th>A</th><th>B</th></tr>"
                                "<tr><td>1</td><td>2</td></tr></table>", 40);

    REQUIRE(!lines.empty());
    for (const std::string &line : lines)
        for (unsigned char c : line)
            CHECK_FALSE(is_e_control(c));
}

TEST_CASE("ASCII stays the default until a client asks for native graphics")
{
    const std::string html = "<h1>Title</h1><ul><li>one</li></ul>";

    // A renderer nobody has told anything about character sets: exactly the
    // rendering every bus produced before native graphics existed.
    FNPretty def;
    def.setLineEnding("\n");
    def.setScreenWidth(40);
    REQUIRE(def.renderDocument(html));
    auto lines = split_lines(def.rendered());

    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "               +-------+");
    CHECK(lines[1] == "               | Title |");
    CHECK(lines[2] == "               +-------+");
    CHECK(lines[3] == "");
    CHECK(lines[4] == "");
    CHECK(lines[5] == "  * one");

    // Declaring the platform is not the same as selecting it - the SIO device
    // declares ATASCII at open, and until a client asks for it the output is
    // byte-for-byte the ASCII rendering above.
    FNPretty declared;
    declared.setLineEnding("\n");
    declared.setScreenWidth(40);
    declared.setNativeCharset(PrettyCharset::Atascii);
    REQUIRE(declared.renderDocument(html));
    CHECK(declared.rendered() == def.rendered());
}

TEST_CASE("setOption: !gfx selects and clears the platform's native graphics")
{
    FNPretty renderer;
    renderer.setNativeCharset(PrettyCharset::Atascii);
    CHECK(renderer.charset() == PrettyCharset::Ascii); // declared, not selected

    CHECK(renderer.setOption("!gfx=1") == PrettyOption::Applied);
    CHECK(renderer.charset() == PrettyCharset::Atascii);
    CHECK(renderer.nativeGraphics());

    // Keys are matched case-insensitively; 8-bit hosts commonly send caps.
    CHECK(renderer.setOption("!GFX=0") == PrettyOption::Applied);
    CHECK(renderer.charset() == PrettyCharset::Ascii);
    CHECK_FALSE(renderer.nativeGraphics());

    // gfx is a flag, not a charset id - a client cannot name a foreign set.
    CHECK(renderer.setOption("!gfx=2") == PrettyOption::Invalid);
    CHECK(renderer.charset() == PrettyCharset::Ascii);
}

TEST_CASE("setOption: !gfx=1 is a harmless no-op on a bus with no native set")
{
    // rs232/iwm/adamnet/drivewire never declare one, so asking for native
    // graphics there leaves the ASCII rendering alone rather than failing.
    FNPretty renderer;
    CHECK(renderer.nativeCharset() == PrettyCharset::Ascii);
    CHECK(renderer.setOption("!gfx=1") == PrettyOption::Applied);
    CHECK(renderer.charset() == PrettyCharset::Ascii);
}

TEST_CASE("setOption: !gfx=1 followed by rerender switches an existing page")
{
    FNPretty renderer;
    renderer.setLineEnding("\n");
    renderer.setScreenWidth(40);
    renderer.setNativeCharset(PrettyCharset::Atascii);
    REQUIRE(renderer.renderDocument("<ul><li>one</li></ul>"));
    CHECK(renderer.rendered() == "  * one"); // the final EOL is trimmed

    CHECK(renderer.setOption("!gfx=1") == PrettyOption::Applied);
    CHECK(renderer.rerender());
    CHECK(renderer.rendered() == "  " + gl(0x14) + " one");
}


// ============================================================================
// Apple II MouseText: wire byte n is glyph $40+n, bit 7 is inverse
// ============================================================================

static std::vector<std::string> render_mousetext(const std::string &html, uint8_t width)
{
    FNPretty renderer;
    RenderOptions opts;
    opts.width = width;
    opts.charset = PrettyCharset::MouseText;
    return render_lines(renderer, html, opts);
}

TEST_CASE("MouseText h1 is a solid inverse block, as on the Atari")
{
    auto lines = render_mousetext("<h1>Title</h1>", 40);

    REQUIRE(lines.size() == 3);
    std::string pad(15, ' ');
    CHECK(lines[0] == pad + inv(std::string(9, ' ')));
    CHECK(lines[1] == pad + inv("  Title  "));
    CHECK(lines[2] == pad + inv(std::string(9, ' ')));
}

TEST_CASE("MouseText h2 is framed with edge bars and underscores")
{
    auto lines = render_mousetext("<h2>Title</h2>", 40);

    // The top edge is at the foot of its row, so a blank framed row
    // follows it to match the gap the bottom edge leaves.
    REQUIRE(lines.size() == 4);
    std::string pad(15, ' ');
    CHECK(lines[0] == pad + "_" + rep('_', 7) + " ");
    CHECK(lines[1] == pad + gl(0x1F) + std::string(7, ' ') + gl(0x1F));
    CHECK(lines[2] == pad + gl(0x1F) + " Title " + gl(0x1F));
    CHECK(lines[3] == pad + gl(0x14) + rep('_', 7) + gl(0x1F));
}

TEST_CASE("MouseText h3 is inverse; h4-h6 underline with MouseText bars")
{
    auto lines = render_mousetext("<h3>Three</h3><h4>Four</h4>"
                                  "<h5>Five</h5><h6>Six</h6>", 40);

    REQUIRE(lines.size() == 10);
    CHECK(lines[0] == inv("Three"));
    CHECK(lines[2] == "Four");
    CHECK(lines[3] == rep(0x0C, 4));
    CHECK(lines[5] == "Five");
    CHECK(lines[6] == rep(0x13, 4));
    CHECK(lines[9] == rep(0x13, 3));
}

TEST_CASE("MouseText bullets are the diamond; inverse keeps } and ~")
{
    auto lines = render_mousetext("<ul><li>one</li></ul><h3>a}b~c</h3>", 40);

    REQUIRE(lines.size() >= 3);
    CHECK(lines[0] == "  " + gl(0x1B) + " one");
    // The Atari's } / ~ fold is an E: workaround; the Apple shows both.
    CHECK(lines.back() == inv("a}b~c"));
}

TEST_CASE("MouseText furniture never uses the Apple EOL or a byte the client treats as space")
{
    // CR is the Apple line ending; tab, LF and 0x9B are whitespace to the
    // fujireader wrapper. None may appear in any glyph or its inverse.
    auto lines = render_mousetext("<h1>T</h1><h2>T</h2><h4>T</h4><h5>T</h5><hr>"
                                  "<ul><li>x</li></ul>"
                                  "<table><tr><td>a</td><td>b</td></tr>"
                                  "<tr><td>c</td><td>d</td></tr></table>", 40);
    for (const std::string &l : lines)
        for (char c : l)
        {
            unsigned char u = (unsigned char)c;
            CHECK(u != 0x0D);
            CHECK(u != 0x0A);
            CHECK(u != 0x09);
            CHECK(u != 0x9B);
        }
}
