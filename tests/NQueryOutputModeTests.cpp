#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <vector>
#include <algorithm>
#include "sio/sioNetwork.h"

#include "NParser.h"
#include "XMLParser.h"
#include "PrettyParser.h"
#include "fn_render_text.h"
#include "fntext/fn_query_flags.h"

systemBus SYSTEM_BUS;

// utils.cpp (reached through PeoplesUrlParser) references the SAM speech engine.
int sam(int argc, char **argv) { (void)argc; (void)argv; return 0; }

namespace {

// A feed with the things an 8-bit host chokes on: curly quotes, an em dash, a
// named entity, a numeric reference, and a namespaced element.
const char *kFeed =
    "<?xml version=\"1.0\"?>\n"
    "<rss xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
    "<channel>"
    "<item><title>Caf\xc3\xa9 \xe2\x80\x94 R&amp;D&#8217;s \xe2\x80\x9c" "best" "\xe2\x80\x9d</title></item>"
    "<item><title>Second</title></item>"
    "<item><title>Third</title></item>"
    "<dc:creator>Ann</dc:creator>"
    "</channel></rss>";

// Serves a canned document to the parser, one read at a time, the way a real
// protocol drains a socket.
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

// Drives one query the way the device does: NET_SET_PARAMETER, then NET_QUERY,
// then read what landed in the receive buffer.
std::string query(XMLParser &p, std::string &rx, const std::string &path, uint8_t flags)
{
    rx.clear();
    p.setQueryParam(flags);
    p.setQuery(path);
    return rx;
}

// A whole session against the canned feed. Repeating a path advances the match
// index, so comparing two output modes needs a session each, not two queries.
struct Session
{
    std::string rx, tx, sp;
    CannedProtocol proto;
    XMLParser parser;

    explicit Session(const char *body = kFeed)
        : proto(&rx, &tx, &sp, body), parser(&proto) {}

    std::string query(const std::string &path, uint8_t flags)
    {
        return ::query(parser, rx, path, flags);
    }
};

} // namespace

TEST_CASE("verbatim is the default and hands back the document's own bytes")
{
    Session s;
    REQUIRE(s.parser.parse().is_success());

    std::string raw = s.query("/rss/channel/item[1]/title", FN_QUERY_OUTPUT_VERBATIM);
    CHECK(raw.find("Caf\xc3\xa9") != std::string::npos);  // UTF-8 e-acute survives
    CHECK(raw.find("\xe2\x80\x94") != std::string::npos); // em dash survives
    CHECK(raw.find("R&D") != std::string::npos);          // tinyxml2 decoded &amp;
}

TEST_CASE("the ASCII flag survives setQuery and reaches the output")
{
    Session s;
    REQUIRE(s.parser.parse().is_success());

    // This is the assert that fails if setQuery() ever goes back to passing a
    // hardcoded 0 into setReadQuery() and wiping the flags.
    std::string ascii = s.query("/rss/channel/item[1]/title", FN_QUERY_OUTPUT_ASCII);
    CHECK(ascii.find("Cafe ") != std::string::npos);
    CHECK(ascii.find("--") != std::string::npos);
    CHECK(ascii.find("R&D's \"best\"") != std::string::npos);
    const std::string eol = SYSTEM_BUS.nativeEOL();
    for (unsigned char c : ascii)
        CHECK(((c >= 0x20 && c <= 0x7E) || eol.find(c) != std::string::npos));
}

TEST_CASE("repeating a query walks to the next match and then runs out")
{
    Session s;
    REQUIRE(s.parser.parse().is_success());

    CHECK(s.query("//item/title", 0).find("Caf") != std::string::npos);
    CHECK(s.query("//item/title", 0).find("Second") != std::string::npos);
    CHECK(s.query("//item/title", 0).find("Third") != std::string::npos);
    CHECK(s.query("//item/title", 0).empty());

    // A different path restarts iteration rather than continuing it.
    CHECK(s.query("//dc:creator", 0).find("Ann") != std::string::npos);
    // ... and a bare local name still finds the prefixed element.
    CHECK(s.query("//creator", 0).find("Ann") != std::string::npos);
}

TEST_CASE("a malformed document reports a parse error rather than an empty channel")
{
    Session s("<rss><channel><item>unclosed");

    CHECK(s.parser.parse().is_error());
}

TEST_CASE("only the defined query flags are accepted")
{
    CHECK(fn_query_param_is_valid(FN_QUERY_OUTPUT_VERBATIM));
    CHECK(fn_query_param_is_valid(FN_QUERY_OUTPUT_ASCII));
    CHECK(fn_query_param_is_valid(FN_QUERY_REMAP_CHARS | FN_QUERY_REMAP_ATASCII_INTERNATIONAL));
    CHECK(fn_query_param_is_valid(FN_QUERY_DELETE_SGML_TAGS)); // 0x04, rejected before
    CHECK(fn_query_param_is_valid(FN_QUERY_DELETE_SGML_TAGS | FN_QUERY_OUTPUT_ASCII));

    CHECK_FALSE(fn_query_param_is_valid(0x08)); // unassigned low-nibble bit
    CHECK(fn_query_param_is_valid(FN_QUERY_OUTPUT_TEXT));
    CHECK(fn_query_param_is_valid(0x20));
    CHECK(fn_query_param_is_valid(FN_QUERY_OUTPUT_TEXT | FN_QUERY_DELETE_SGML_TAGS));
    CHECK_FALSE(fn_query_param_is_valid(0x30)); // reserved output mode
    CHECK_FALSE(fn_query_param_is_valid(0x40)); // above the field
}

TEST_CASE("the remap flag converts every occurrence of a character, not just the first")
{
    Session session("<r><t>M\xc3\xbcller \xc3\xbc" "ber</t></r>");
    REQUIRE(session.parser.parse().is_success());

    std::string remapped = session.query("/r/t", FN_QUERY_REMAP_CHARS);
    CHECK(remapped.find("Mueller ueber") != std::string::npos);
}

// ----------------------------------------------------------------------------
// TEXT output mode (0x20): the queried value is HTML, rendered to wrapped ASCII.
// ----------------------------------------------------------------------------

namespace {

// Excerpts of real feeds. The RSS item is shaped like the Hacker News feed (a
// CDATA <description> of HTML); the Atom entry is a Daring Fireball <content>.
const char *kHtmlRss =
    "<rss version=\"2.0\"><channel><title>T</title>"
    "<item><title>Thieves</title><description><![CDATA["
    "<p>Paresh Dave and Aarian Marshall, reporting for Wired "
    "(<a href=\"https://apple.news/AXazcsSUuTOq8F32MB2X88w\">News+ link</a> in case "
    "Wired hits you with their paywall):</p>\n"
    "<blockquote>\n  <p>The incidents involved two different shipments of high-value\n"
    "technology traveling from Silicon Valley to Southern California, according to "
    "Pachuca, who says he learned about the thefts from law enforcement "
    "authorities.</p>\n</blockquote>\n"
    "<p>I called it back in March: <a href=\"https://daringfireball.net/linked/2026/03/31/"
    "ram-is-the-new-bearer-bond\">RAM is the new bearer bond</a>. R&amp;D &#8212; "
    "&ldquo;done&rdquo;&nbsp;&#9733;</p>\n"
    "]]></description></item>"
    "<item><title>Plain</title><description>just words</description></item>"
    "</channel></rss>";

const char *kHtmlAtom =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>DF</title>"
    "<entry><title>Dickover</title>"
    "<content type=\"html\" xml:base=\"https://daringfireball.net/linked/\"><![CDATA[\n"
    "<p>Stuart Heritage, writing for The Guardian, under the splendid headline "
    "\xe2\x80\x9c" "Dickovers, Baggravation and Botiquette\xe2\x80\x9d:</p>\n\n"
    "<ul><li>baggravation</li><li>Schr\xc3\xb6" "dinger\xe2\x80\x99s Wi-Fi</li></ul>\n"
    "<div>\n<a title=\"Permanent link\" href=\"https://daringfireball.net/\">&nbsp;&#9733;&nbsp;</a>\n</div>\n"
    "\t]]></content></entry></feed>";

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    size_t start = 0;
    const std::string eol = SYSTEM_BUS.nativeEOL();
    while (true)
    {
        size_t nl = text.find(eol, start);
        if (nl == std::string::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + eol.size();
    }
    return lines;
}

size_t longestLine(const std::string &text)
{
    size_t longest = 0;
    for (const std::string &l : splitLines(text))
        longest = std::max(longest, l.size());
    return longest;
}

// Line breaks as single spaces, for finding a phrase the wrapper may have split.
std::string unwrapped(const std::string &v)
{
    std::string out;
    for (const std::string &l : splitLines(v))
        out += (out.empty() ? "" : " ") + l;
    return out;
}

// The value without the parser's trailing line ending (the bus's EOL, #2336).
std::string stripEol(std::string v)
{
    const std::string eol = SYSTEM_BUS.nativeEOL();
    REQUIRE(v.size() >= eol.size());
    REQUIRE(v.compare(v.size() - eol.size(), eol.size(), eol) == 0);
    v.resize(v.size() - eol.size());
    return v;
}

} // namespace

TEST_CASE("TEXT mode renders a CDATA HTML description as wrapped text without tags")
{
    Session s(kHtmlRss);
    REQUIRE(s.parser.parse().is_success());

    std::string out = stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT));

    CHECK(out.find('<') == std::string::npos);
    CHECK(out.find('>') == std::string::npos);
    CHECK(out.find("&amp;") == std::string::npos);
    CHECK(out.find("&#") == std::string::npos);
    CHECK(out.find("&nbsp;") == std::string::npos);
    CHECK(out.find("R&D") != std::string::npos);           // entity decoded
    CHECK(out.find("--") != std::string::npos);            // em dash folded to ASCII
    CHECK(out.find("\"done\"") != std::string::npos);      // curly quotes folded
    CHECK(unwrapped(out).find("News+ link") != std::string::npos);    // link text kept
    CHECK(out.find("paywall") != std::string::npos);
    CHECK(unwrapped(out).find("RAM is the new bearer bond") != std::string::npos);
    CHECK(out.find("https://") == std::string::npos);      // links are not rendered
    CHECK(splitLines(out).size() > 1);                     // wrapped into several lines
    CHECK(longestLine(out) <= 40);                         // default width

    for (const std::string &l : splitLines(out))
        for (unsigned char c : l)
            CHECK((c >= 0x20 && c <= 0x7E));
}

TEST_CASE("TEXT mode breaks and ends its lines with the parser's line ending")
{
    Session s(kHtmlRss);
    REQUIRE(s.parser.parse().is_success());
    s.parser.setLineEnding("\x9b");

    std::string out = s.query("//item/description", FN_QUERY_OUTPUT_TEXT);
    REQUIRE(out.size() > 2);
    CHECK((unsigned char) out.back() == 0x9b);
    // The wrapped lines are broken with it too, there is no 0x0A left, and
    // there is no empty line before the final line ending.
    CHECK(out.find('\x9b') < out.size() - 1);
    CHECK(out.find('\n') == std::string::npos);
    CHECK((unsigned char) out[out.size() - 2] != 0x9b);
}

TEST_CASE("TEXT mode breaks lines with a multi-byte line ending")
{
    Session s(kHtmlRss);
    REQUIRE(s.parser.parse().is_success());
    s.parser.setLineEnding("\r\n");

    std::string out = s.query("//item/description", FN_QUERY_OUTPUT_TEXT);
    REQUIRE(out.size() > 2);
    CHECK(out.compare(out.size() - 2, 2, "\r\n") == 0);
    CHECK(out.find("\r\n") < out.size() - 2);
    // Every 0x0A is the tail of a CR LF.
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] == '\n')
            CHECK((i > 0 && out[i - 1] == '\r'));
}

TEST_CASE("the text width parameter sets where TEXT mode wraps")
{
    std::string w32, w40, w20;
    {
        Session s(kHtmlRss);
        REQUIRE(s.parser.parse().is_success());
        REQUIRE(s.parser.setTextWidth(32).is_success());
        w32 = stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT));
    }
    {
        Session s(kHtmlRss);
        REQUIRE(s.parser.parse().is_success());
        w40 = stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT));
    }
    {
        Session s(kHtmlRss);
        REQUIRE(s.parser.parse().is_success());
        REQUIRE(s.parser.setTextWidth(20).is_success());
        w20 = stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT));
    }

    CHECK(longestLine(w32) <= 32);
    CHECK(longestLine(w40) <= 40);
    CHECK(longestLine(w20) <= 20);
    CHECK(longestLine(w32) > 20); // it really used the room it was given
    CHECK(longestLine(w40) > 32);
    // Narrower means more lines.
    CHECK(splitLines(w32).size() > splitLines(w40).size());
    CHECK(splitLines(w20).size() > splitLines(w32).size());
    // Width 0 is the 40 column default.
    {
        Session s(kHtmlRss);
        REQUIRE(s.parser.parse().is_success());
        s.parser.setTextWidth(32);
        s.parser.setTextWidth(0);
        CHECK(stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT)) == w40);
    }
}

TEST_CASE("TEXT mode on an Atom <content> entry renders lists and folds Unicode")
{
    Session s(kHtmlAtom);
    REQUIRE(s.parser.parse().is_success());
    s.parser.setTextWidth(32);

    std::string out = stripEol(s.query("//entry/content", FN_QUERY_OUTPUT_TEXT));
    CHECK(out.find('<') == std::string::npos);
    CHECK(out.find("&nbsp;") == std::string::npos);
    CHECK(unwrapped(out).find("Dickovers, Baggravation") != std::string::npos);
    CHECK(out.find("baggravation") != std::string::npos);
    CHECK(unwrapped(out).find("Schrodinger's Wi-Fi") != std::string::npos);
    CHECK(longestLine(out) <= 32);
    for (const std::string &l : splitLines(out))
        for (unsigned char c : l)
            CHECK((c >= 0x20 && c <= 0x7E));
}

TEST_CASE("TEXT mode passes a value that is not markup through as wrapped text")
{
    Session s(kHtmlRss);
    REQUIRE(s.parser.parse().is_success());

    s.query("//item/description", FN_QUERY_OUTPUT_TEXT); // first item
    CHECK(stripEol(s.query("//item/description", FN_QUERY_OUTPUT_TEXT)) == "just words");
}

TEST_CASE("fn_render_html_text has no trailing break and honours the width")
{
    const std::string html = "<p>one two three four five six seven eight nine ten</p><p>last</p>";

    const std::string eol = SYSTEM_BUS.nativeEOL();
    std::string out = fn_render_html_text(html, 16, eol);
    CHECK(out.compare(out.size() - eol.size(), eol.size(), eol) != 0);
    CHECK(splitLines(out).size() > 1);
    CHECK(longestLine(out) <= 16);
    CHECK(out.find("last") != std::string::npos);
    CHECK(out.find('\n') == std::string::npos);
    CHECK(out.find('\r') == std::string::npos);

    CHECK(fn_render_html_text("", 40, eol).empty());
    // With "\n" the lines are exactly the renderer's.
    CHECK(fn_render_html_text(html, 16, "\n").find('\n') != std::string::npos);
}

// ----------------------------------------------------------------------------
// PrettyParser: PARSER::PRETTY, the whole page read back sequentially.
// ----------------------------------------------------------------------------

namespace {

const char *kPage =
    "<html><head><title>Page</title></head><body>"
    "<h1>Heading</h1>"
    "<p>First paragraph with a <a href=\"/next\">relative link</a> and "
    "<a href=\"https://example.com/abs\">an absolute one</a>.</p>"
    "<div id=\"sec\"><p>Only this section.</p></div>"
    "</body></html>";

struct PrettySession
{
    std::string rx, tx, sp;
    CannedProtocol proto;
    PeoplesUrlParser url;
    PrettyParser parser;

    explicit PrettySession(const char *body = kPage)
        : proto(&rx, &tx, &sp, body), url("https://example.com/dir/page.html"), parser(&proto)
    {
        proto.opened_url = &url;
        proto.native_eol = SYSTEM_BUS.nativeEOL(); // what NDevice sets on every protocol
    }

    std::string drain(size_t chunk = 16)
    {
        std::string all, piece;
        while (parser.read(piece, chunk) == FUJI_ERROR::NONE)
            all += piece;
        return all;
    }
};

} // namespace

TEST_CASE("PRETTY parse renders the page and read serves it sequentially until EOF")
{
    PrettySession s;
    CHECK(s.parser.status().error == NDEV_STATUS::END_OF_FILE); // nothing parsed yet
    REQUIRE(s.parser.parse().is_success());

    size_t total = s.parser.available();
    REQUIRE(total > 0);
    NetworkStatus ns = s.parser.status();
    CHECK(ns.connected);
    CHECK(ns.error == NDEV_STATUS::SUCCESS);

    std::string first;
    REQUIRE(s.parser.read(first, 10) == FUJI_ERROR::NONE);
    CHECK(first.size() == 10);
    CHECK(s.parser.available() == total - 10);

    std::string rest = s.drain();
    std::string all = first + rest;
    CHECK(all.size() == total);
    CHECK(all.find("Heading") != std::string::npos);
    CHECK(all.find("Only this section.") != std::string::npos);
    CHECK(all.find('<') == std::string::npos);

    // Drained: available is 0, STATUS says EOF, READ fails.
    CHECK(s.parser.available() == 0);
    CHECK(s.parser.status().error == NDEV_STATUS::END_OF_FILE);
    CHECK_FALSE(s.parser.status().connected);
    std::string none;
    CHECK(s.parser.read(none, 8) != FUJI_ERROR::NONE);
}

TEST_CASE("PRETTY uses the bus's native line ending unless told otherwise")
{
    PrettySession s;
    REQUIRE(s.parser.parse().is_success());
    std::string a = s.drain();
    CHECK(a.find(SYSTEM_BUS.nativeEOL()) != std::string::npos);

    PrettySession t;
    REQUIRE(t.parser.setLineEnding("|").is_success());
    REQUIRE(t.parser.parse().is_success());
    std::string b = t.drain();
    CHECK(b.find('|') != std::string::npos);
    CHECK(b.find(SYSTEM_BUS.nativeEOL()) == std::string::npos);
}

TEST_CASE("PRETTY width comes from the text width parameter or a !width option")
{
    PrettySession a;
    REQUIRE(a.parser.setTextWidth(24).is_success());
    REQUIRE(a.parser.parse().is_success());
    std::string text24 = a.drain();

    PrettySession b;
    REQUIRE(b.parser.parse().is_success());
    REQUIRE(b.parser.setQuery("!width=24").is_success());
    std::string text24b = b.drain();

    CHECK(text24 == text24b);
    CHECK(text24.size() > 0);

    std::string line;
    size_t longest = 0;
    for (char c : text24)
    {
        if (c == SYSTEM_BUS.nativeEOL()[0])
        {
            longest = std::max(longest, line.size());
            line.clear();
        }
        else
            line += c;
    }
    CHECK(longest <= 24);

    PrettySession c;
    CHECK(c.parser.setQuery("!bogus=1").is_error());
}

TEST_CASE("PRETTY CSS selector narrows the page and an empty one restores it")
{
    PrettySession s;
    REQUIRE(s.parser.parse().is_success());
    std::string whole = s.drain();

    REQUIRE(s.parser.setQuery("#sec").is_success());
    std::string section = s.drain();
    CHECK(section.find("Only this section.") != std::string::npos);
    CHECK(section.find("Heading") == std::string::npos);

    REQUIRE(s.parser.setQuery("").is_success());
    CHECK(s.drain() == whole);
}

TEST_CASE("PRETTY numeric query stages a link URL ahead of the page, then the page resumes")
{
    PrettySession s;
    REQUIRE(s.parser.parse().is_success());
    REQUIRE(s.parser.setQuery("!links=1").is_success());

    std::string head;
    REQUIRE(s.parser.read(head, 5) == FUJI_ERROR::NONE);
    size_t pageLeft = s.parser.available();

    // Link 1 is the relative one, resolved against the opened URL.
    REQUIRE(s.parser.setQuery("1").is_success());
    std::string url = "https://example.com/next" + SYSTEM_BUS.nativeEOL();
    CHECK(s.parser.available() == url.size());

    std::string got;
    REQUIRE(s.parser.read(got, 1000) == FUJI_ERROR::NONE);
    CHECK(got == url);

    // The page picks up where it left off.
    CHECK(s.parser.available() == pageLeft);

    REQUIRE(s.parser.setQuery("2").is_success());
    REQUIRE(s.parser.read(got, 1000) == FUJI_ERROR::NONE);
    CHECK(got == "https://example.com/abs" + SYSTEM_BUS.nativeEOL());

    // No such link, and index 0, are errors that stage nothing.
    CHECK(s.parser.setQuery("99").is_error());
    CHECK(s.parser.setQuery("0").is_error());
    CHECK(s.parser.available() == pageLeft);
}

TEST_CASE("PRETTY parse fails cleanly with no data")
{
    PrettySession s("");
    CHECK(s.parser.parse().is_error());
    CHECK(s.parser.available() == 0);
    CHECK(s.parser.status().error != NDEV_STATUS::SUCCESS);
}

// ----------------------------------------------------------------------------
// Link stubs. Only XMLParser, NParser and the fnxml/fntext libraries are
// compiled into this target; everything the bus headers drag in behind them is
// satisfied here, the same way sioNetwork-DSTATS-test.cpp does it.
// ----------------------------------------------------------------------------

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS
void TTYChannel::updateFIFO() {}
size_t TTYChannel::dataOut(const void *buffer, size_t length) { (void)buffer; (void)length; return 0; }
void TTYChannel::end() {}
void TTYChannel::flushOutput() {}
void TTYChannel::setBaudrate(uint32_t baud) { (void)baud; }
bool TTYChannel::getDTR() { return false; }
void TTYChannel::setDSR(bool state) { (void)state; }
bool TTYChannel::getRTS() { return false; }
void TTYChannel::setCTS(bool state) { (void)state; }
bool TTYChannel::getDCD() { return false; }
bool TTYChannel::getRI() { return false; }
#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */

#ifdef HELLO_IM_A_PC
void COMChannel::updateFIFO() {}
size_t COMChannel::dataOut(const void *buffer, size_t length) { (void)buffer; (void)length; return 0; }
void COMChannel::end() {}
void COMChannel::flushOutput() {}
void COMChannel::setBaudrate(uint32_t baud) { (void)baud; }
bool COMChannel::getDTR() { return false; }
void COMChannel::setDSR(bool state) { (void)state; }
bool COMChannel::getRTS() { return false; }
void COMChannel::setCTS(bool state) { (void)state; }
bool COMChannel::getDCD() { return false; }
bool COMChannel::getRI() { return false; }
#endif /* HELLO_IM_A_PC */

NetSIO::NetSIO()
    : _ip(0), _port(0), _baud(0), _baud_peer(0), _fd(-1),
      _initialized(false), _command_asserted(false), _motor_asserted(false),
      _sync_request_num(-1), _sync_ack_byte(0), _sync_write_size(0),
      _errcount(0), _resume_time(0), _alive_time(0), _alive_request(0),
      _credit(0)
{
}
NetSIO::~NetSIO() {}
void NetSIO::updateFIFO() {}
size_t NetSIO::dataOut(const void *buffer, size_t length) { (void)buffer; (void)length; return 0; }
void NetSIO::end() {}
void NetSIO::flushOutput() {}
void NetSIO::setBaudrate(uint32_t baud) { (void)baud; }

void virtualDevice::sio_high_speed() {}

void systemBus::transaction_accept(transState_t expectMoreData) {}
void systemBus::transaction_success() {}
void systemBus::transaction_error() {}
size_result_t systemBus::transaction_get_length(void *data, size_t len) { RETURN_SIZE_ERROR(); }
void systemBus::transaction_send(const void *data, size_t len, bool is_error) {}
void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id) {}

void DaisyChain::addDevice(virtualDevice *newDev, fujiDeviceID_t fujiID) {}
void DaisyChain::assignFujiIDToDevice(virtualDevice *device, fujiDeviceID_t fujiID) {}

// CannedProtocol overrides read/status/available; the rest of the base is inert.
NetworkProtocol::NetworkProtocol(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf) {}
NetworkProtocol::~NetworkProtocol() {}
fujiError_t NetworkProtocol::open(PeoplesUrlParser *urlParser, fileAccessMode_t access, netProtoTranslation_t translate)
{
    (void)urlParser; (void)access; (void)translate;
    return FUJI_ERROR::NONE;
}
fujiError_t NetworkProtocol::close() { return FUJI_ERROR::NONE; }
void NetworkProtocol::errno_to_error() {}
fujiError_t NetworkProtocol::read(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocol::status(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
