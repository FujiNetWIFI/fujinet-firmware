#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

#include "NParser.h"
#include "XMLParser.h"
#include "PrettyParser.h"
#include "bus.h"

// Built into nquery_output_mode_tests, which provides main() and the link stubs.

namespace {

const char *kRss =
    "<?xml version=\"1.0\"?>"
    "<rss><channel><title>Channel Title</title>"
    "<item><title>One</title><description>"
    "&lt;p&gt;Hello &lt;a href=\"http://x.example/a\"&gt;first link&lt;/a&gt; world&lt;/p&gt;"
    "</description></item>"
    "<item><title>Two</title><description>"
    "<![CDATA[<p>Other <a href=\"/rel\">rel link</a></p>]]>"
    "</description></item>"
    "</channel></rss>";

const char *kPage = "<html><body><p>Fetched page</p></body></html>";

// Serves a canned body from the protocol stream, as a drained socket would.
class BodyProtocol : public NetworkProtocol
{
public:
    BodyProtocol(std::string *rx, std::string *tx, std::string *sp, const char *body)
        : NetworkProtocol(rx, tx, sp), _body(body)
    {
        receiveBuffer = rx;
        transmitBuffer = tx;
    }

    fujiError_t read(unsigned short len) override
    {
        size_t n = std::min((size_t) len, _body.size() - _pos);
        receiveBuffer->append(_body, _pos, n);
        _pos += n;
        return FUJI_ERROR::NONE;
    }

    fujiError_t status(NetworkStatus *s) override
    {
        s->connected = _pos < _body.size();
        s->error = NDEV_STATUS::SUCCESS;
        return FUJI_ERROR::NONE;
    }

    size_t available() override { return _body.size() - _pos; }

private:
    std::string _body;
    size_t _pos = 0;
};

struct Fixture
{
    std::string rx, tx, sp;
    BodyProtocol proto;

    explicit Fixture(const char *body) : proto(&rx, &tx, &sp, body) {}
};

std::string readAll(NParser &p)
{
    std::string all, chunk;
    while (p.available() > 0 && p.read(chunk, 4096) == FUJI_ERROR::NONE)
        all += chunk;
    return all;
}

} // namespace

TEST_CASE("a query replaces the unread data of the one before")
{
    Fixture f(kRss);
    XMLParser xml(&f.proto);
    REQUIRE(xml.parse().is_success());

    const std::string eol = SYSTEM_BUS.nativeEOL();
    REQUIRE(xml.setQuery("/rss/channel/title").is_success());
    CHECK(f.rx == "Channel Title" + eol);

    // A partial read leaves the rest unread; the next query drops it.
    std::string part;
    REQUIRE(xml.read(part, 4) == FUJI_ERROR::NONE);
    CHECK(part == "Chan");
    REQUIRE(xml.setQuery("/rss/channel/item[1]/title").is_success());
    CHECK(f.rx == "One" + eol);
}

TEST_CASE("seek clears, repositions within the result, and tell reports the position")
{
    Fixture f(kRss);
    XMLParser xml(&f.proto);

    // No query yet: the result is empty and seek succeeds at 0.
    CHECK(xml.seek(0, SEEK_SET) == 0);

    REQUIRE(xml.parse().is_success());
    const std::string eol = SYSTEM_BUS.nativeEOL();
    const std::string whole = "Channel Title" + eol;
    REQUIRE(xml.setQuery("/rss/channel/title").is_success());
    CHECK(xml.seek(0, SEEK_CUR) == 0);

    CHECK(xml.seek(3, SEEK_SET) == 3);
    CHECK(f.rx == whole.substr(3));
    CHECK(xml.seek(0, SEEK_CUR) == 3);

    std::string part;
    REQUIRE(xml.read(part, 2) == FUJI_ERROR::NONE);
    CHECK(xml.seek(0, SEEK_CUR) == 5);

    // Back to the start restores the whole result.
    CHECK(xml.seek(0, SEEK_SET) == 0);
    CHECK(f.rx == whole);

    // The bus sends 0xFFFFFFFF to clear.
    CHECK(xml.seek((off_t) 0xFFFFFFFFu, SEEK_SET) == (off_t) whole.size());
    CHECK(f.rx.empty());
    CHECK(xml.seek(0, SEEK_CUR) == (off_t) whole.size());

    // Other whence values are refused.
    CHECK(xml.seek(0, SEEK_END) == -1);
    CHECK(xml.seek(1, SEEK_CUR) == -1);
}

TEST_CASE("a query value's raw form is the document's, whatever the query flags")
{
    Fixture f(kRss);
    XMLParser xml(&f.proto);
    std::string raw;

    CHECK_FALSE(xml.rawValue(raw)); // no document, no query
    REQUIRE(xml.parse().is_success());
    CHECK_FALSE(xml.rawValue(raw)); // parsed, no query yet

    xml.setQueryParam(0x10); // ASCII output
    REQUIRE(xml.setQuery("/rss/channel/item[1]/description").is_success());
    REQUIRE(xml.rawValue(raw));
    CHECK(raw == "<p>Hello <a href=\"http://x.example/a\">first link</a> world</p>");

    // Asking does not disturb what was served or advance the match.
    CHECK(f.rx.find("Hello") != std::string::npos);
    std::string again;
    REQUIRE(xml.rawValue(again));
    CHECK(again == raw);

    // A CDATA section arrives as its markup text.
    xml.setQueryParam(0);
    REQUIRE(xml.setQuery("/rss/channel/item[2]/description").is_success());
    REQUIRE(xml.rawValue(raw));
    CHECK(raw == "<p>Other <a href=\"/rel\">rel link</a></p>");

    // A new PARSE forgets the query.
    Fixture g(kRss);
    XMLParser fresh(&g.proto);
    REQUIRE(fresh.parse().is_success());
    CHECK_FALSE(fresh.rawValue(raw));
}

TEST_CASE("PRETTY over a query value renders only that value, with links")
{
    Fixture f(kRss);
    auto xml = std::make_unique<XMLParser>(&f.proto);
    REQUIRE(xml->parse().is_success());
    REQUIRE(xml->setQuery("/rss/channel/item[1]/description").is_success());

    // What NDevice::fujicore_set_parser does for PRETTY after a query.
    std::string value;
    REQUIRE(xml->rawValue(value));
    f.rx.clear();
    PrettyParser pretty(&f.proto, std::move(xml), value);
    CHECK(pretty.mode() == PARSER::PRETTY);
    REQUIRE(pretty.savedParser() != nullptr);
    CHECK(pretty.savedParser()->mode() == PARSER::XML);

    REQUIRE(pretty.setRenderLinks(true).is_success());
    REQUIRE(pretty.parse().is_success());

    std::string text = readAll(pretty);
    CHECK(text.find("Hello") != std::string::npos);
    CHECK(text.find("first link") != std::string::npos);
    CHECK(text.find("[1]") != std::string::npos);
    CHECK(text.find("Channel Title") == std::string::npos);
    CHECK(text.find("One") == std::string::npos);

    // Link lookup serves the URL, then the page resumes.
    REQUIRE(pretty.setQuery("1").is_success());
    CHECK(readAll(pretty).find("http://x.example/a") == 0);
    CHECK(pretty.setQuery("2").is_error());

    // Sections work as on a page.
    REQUIRE(pretty.setQuery("p").is_success());
    CHECK(readAll(pretty).find("Hello") != std::string::npos);
    REQUIRE(pretty.setQuery("a@href").is_success());
    CHECK(readAll(pretty).find("http://x.example/a") == 0);
}

TEST_CASE("PRETTY over a CDATA value works, and restoring the parser keeps its document")
{
    Fixture f(kRss);
    auto xml = std::make_unique<XMLParser>(&f.proto);
    REQUIRE(xml->parse().is_success());
    REQUIRE(xml->setQuery("/rss/channel/item[2]/description").is_success());

    std::string value;
    REQUIRE(xml->rawValue(value));
    PrettyParser pretty(&f.proto, std::move(xml), value);
    pretty.setRenderLinks(true);
    REQUIRE(pretty.parse().is_success());
    CHECK(readAll(pretty).find("rel link") != std::string::npos);
    CHECK(pretty.setQuery("1").is_success());

    // Back to XML: the same document answers a further query.
    std::unique_ptr<NParser> restored = pretty.takeSavedParser();
    REQUIRE(restored != nullptr);
    CHECK(restored->mode() == PARSER::XML);
    CHECK(pretty.savedParser() == nullptr);
    REQUIRE(restored->setQuery("/rss/channel/item[1]/title").is_success());
    CHECK(f.rx == "One" + SYSTEM_BUS.nativeEOL());
}

TEST_CASE("PRETTY with no query parser still fetches the page")
{
    Fixture f(kPage);
    PrettyParser pretty(&f.proto);

    CHECK(pretty.savedParser() == nullptr);
    REQUIRE(pretty.parse().is_success());
    CHECK(readAll(pretty).find("Fetched page") != std::string::npos);
}
