#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "fnxml/fnxml_query.h"
#include "tinyxml2.h"

#include <string>

namespace {

const char *kFeed =
    "<?xml version=\"1.0\"?>\n"
    "<rss xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
    "<channel><title>Feed</title>"
    "<item id=\"a\" href=\"http://example.com/1\"><title>One</title><dc:creator>Ann</dc:creator></item>"
    "<item id=\"b\"><title>Two</title><desc>pre<b>mid</b>post</desc></item>"
    "<item id=\"c\"><title>Three</title></item>"
    "</channel></rss>";

// Resolves query against doc and returns the matchIndex'th value, or "" for no match.
std::string q(tinyxml2::XMLDocument &doc, const char *query, size_t matchIndex = 0)
{
    std::string out;
    if (!fnxml_resolve_query(&doc, query, matchIndex, out))
        return "";
    return out;
}

} // namespace

TEST_CASE("fnxml query")
{
    tinyxml2::XMLDocument doc;
    REQUIRE(doc.Parse(kFeed) == tinyxml2::XML_SUCCESS);

    SUBCASE("absolute and relative paths both start at the root element")
    {
        CHECK(q(doc, "/rss/channel/title") == "Feed");
        CHECK(q(doc, "rss/channel/title") == "Feed");
        CHECK(q(doc, "  /rss/channel/title  ") == "Feed");
    }

    SUBCASE("repeating a query walks the matches, then runs out")
    {
        CHECK(q(doc, "/rss/channel/item/title", 0) == "One");
        CHECK(q(doc, "/rss/channel/item/title", 1) == "Two");
        CHECK(q(doc, "/rss/channel/item/title", 2) == "Three");
        CHECK(q(doc, "/rss/channel/item/title", 3) == "");
    }

    SUBCASE("descendant-or-self at the start and mid-path")
    {
        CHECK(q(doc, "//title", 0) == "Feed");
        CHECK(q(doc, "//title", 1) == "One");
        CHECK(q(doc, "/rss//title", 0) == "Feed");
        CHECK(q(doc, "/rss//title", 1) == "One");
    }

    SUBCASE("wildcard step")
    {
        CHECK(q(doc, "/rss/channel/*", 0) == "Feed");
        CHECK(q(doc, "/rss/channel/*", 1) == "OneAnn");
    }

    SUBCASE("positional predicate")
    {
        CHECK(q(doc, "/rss/channel/item[2]/title") == "Two");
        CHECK(q(doc, "//item[3]/title") == "Three");
        CHECK(q(doc, "/rss/channel/item[9]/title") == "");
    }

    SUBCASE("attribute predicates")
    {
        CHECK(q(doc, "/rss/channel/item[@href]/title") == "One");
        CHECK(q(doc, "/rss/channel/item[@id='b']/title") == "Two");
        CHECK(q(doc, "/rss/channel/item[@id=\"c\"]/title") == "Three");
        CHECK(q(doc, "/rss/channel/item[@id='zzz']/title") == "");
    }

    SUBCASE("a '/' inside a predicate value is not a step separator")
    {
        CHECK(q(doc, "//item[@href='http://example.com/1']/title") == "One");
    }

    SUBCASE("text() predicate")
    {
        CHECK(q(doc, "//title[text()='Two']") == "Two");
        CHECK(q(doc, "//title[text()='nope']") == "");
    }

    SUBCASE("trailing attribute selector, both spellings")
    {
        CHECK(q(doc, "/rss/channel/item[@id='a']@href") == "http://example.com/1");
        CHECK(q(doc, "/rss/channel/item[@id='a']/@href") == "http://example.com/1");
        CHECK(q(doc, "/rss/channel/item/@id", 0) == "a");
        CHECK(q(doc, "/rss/channel/item/@id", 1) == "b");
        CHECK(q(doc, "//item[@id='a']/@missing") == "");
    }

    SUBCASE("namespace prefixes match literally and by local name")
    {
        CHECK(q(doc, "//dc:creator") == "Ann");
        CHECK(q(doc, "//creator") == "Ann");
    }

    SUBCASE("element value is all descendant text, in document order")
    {
        CHECK(q(doc, "//desc") == "premidpost");
    }

    // The query arrives verbatim from an 8-bit host, so garbage must yield no
    // match rather than a crash or a wrong element.
    SUBCASE("malformed queries match nothing")
    {
        CHECK(q(doc, "") == "");
        CHECK(q(doc, "   ") == "");
        CHECK(q(doc, "///") == "");
        CHECK(q(doc, "//") == "");
        CHECK(q(doc, "@") == "");
        CHECK(q(doc, "[]") == "");
        CHECK(q(doc, "/rss/channel/item[") == "");
        CHECK(q(doc, "/rss/channel/item[[[") == "");
        CHECK(q(doc, "/rss/channel/item[@") == "");
        CHECK(q(doc, "/rss/channel/item[bogus()]") == "");
        CHECK(q(doc, "/nope/nope") == "");
    }

    SUBCASE("a null document matches nothing")
    {
        std::string out;
        CHECK(fnxml_resolve_query(nullptr, "/rss", 0, out) == false);
        CHECK(out.empty());
    }
}
