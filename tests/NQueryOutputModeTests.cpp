#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "sio/sioNetwork.h"

#include "NParser.h"
#include "XMLParser.h"
#include "fntext/fn_query_flags.h"

systemBus SYSTEM_BUS;

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
    for (unsigned char c : ascii)
        CHECK(((c >= 0x20 && c <= 0x7E) || c == '\n'));
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
    CHECK_FALSE(fn_query_param_is_valid(0x20)); // reserved output mode
    CHECK_FALSE(fn_query_param_is_valid(0x30)); // reserved output mode
    CHECK_FALSE(fn_query_param_is_valid(0x40)); // above the field
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
success_is_true systemBus::transaction_get(void *data, size_t len) { RETURN_ERROR_AS_FALSE(); }
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
