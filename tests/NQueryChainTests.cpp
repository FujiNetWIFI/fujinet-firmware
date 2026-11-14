#include <doctest/doctest.h>

#include "NParser.h"
#include "XMLParser.h"

// Built into nquery_output_mode_tests, which provides main() and the link stubs.

namespace {

// A feed whose descriptions hold XML as escaped text and as CDATA.
const char *kFeed =
    "<?xml version=\"1.0\"?>\n"
    "<rss><channel>"
    "<item><description>&lt;p&gt;Hello &lt;b&gt;there&lt;/b&gt;&lt;/p&gt;</description></item>"
    "<item><description><![CDATA[<p>Second <i>one</i></p>]]></description></item>"
    "</channel></rss>";

// Serves a canned document, one read at a time, the way a real protocol drains a socket.
class CannedProtocol : public NetworkProtocol
{
public:
    CannedProtocol(std::string *rx, std::string *tx, std::string *sp, const char *body)
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

struct ChainSession
{
    std::string rx, tx, sp;
    CannedProtocol proto;
    XMLParser outer;
    XMLParser inner;

    ChainSession() : proto(&rx, &tx, &sp, kFeed), outer(&proto), inner(&proto) {}

    std::string query(XMLParser &parser, const std::string &path)
    {
        rx.clear();
        parser.setQuery(path);
        return rx;
    }
};

} // namespace

TEST_CASE("rawValue is none before a query and after a re-parse")
{
    ChainSession s;
    REQUIRE(s.outer.parse().is_success());
    CHECK_FALSE(s.outer.rawValue().has_value());

    s.query(s.outer, "//description");
    CHECK(s.outer.rawValue().has_value());

    s.outer.setDocument(kFeed);
    REQUIRE(s.outer.parse().is_success());
    CHECK_FALSE(s.outer.rawValue().has_value());
}

TEST_CASE("rawValue is empty when the query matched nothing")
{
    ChainSession s;
    REQUIRE(s.outer.parse().is_success());

    s.query(s.outer, "//nothing");
    REQUIRE(s.outer.rawValue().has_value());
    CHECK(s.outer.rawValue()->empty());
}

TEST_CASE("an XML parser parses the escaped XML in another's value and leaves it intact")
{
    ChainSession s;
    REQUIRE(s.outer.parse().is_success());

    s.query(s.outer, "//description");
    std::optional<std::string> value = s.outer.rawValue();
    REQUIRE(value.has_value());
    CHECK(*value == "<p>Hello <b>there</b></p>");

    REQUIRE(s.inner.setDocument(std::move(*value)).is_success());
    REQUIRE(s.inner.parse().is_success());
    CHECK(s.query(s.inner, "/p/b").find("there") != std::string::npos);

    // The outer parser's match iteration carries on from where it was.
    s.query(s.outer, "//description");
    CHECK(*s.outer.rawValue() == "<p>Second <i>one</i></p>");
}

TEST_CASE("CDATA in the outer value reaches the inner parser unwrapped")
{
    ChainSession s;
    REQUIRE(s.outer.parse().is_success());

    s.query(s.outer, "//item[2]/description");
    REQUIRE(s.inner.setDocument(*s.outer.rawValue()).is_success());
    REQUIRE(s.inner.parse().is_success());
    CHECK(s.query(s.inner, "/p/i").find("one") != std::string::npos);
}
