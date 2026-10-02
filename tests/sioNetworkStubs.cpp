// Link-time stand-ins for everything sioNetwork and NDevice pull in, shared by
// the tests that build them. The systemBus stubs record into `bus`.

#include "sio/sioNetwork.h"
#include "sioNetworkStubs.h"

systemBus SYSTEM_BUS;
BusLog bus;

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

void systemBus::toggleBaudrate() {}
void systemBus::transaction_accept(transState_t expectMoreData) {}
void systemBus::transaction_success() { bus.success++; }
void systemBus::transaction_error() { bus.error++; }
success_is_true systemBus::transaction_get(void *data, size_t len) { RETURN_ERROR_AS_FALSE(); }
void systemBus::transaction_send(const void *data, size_t len, bool is_error)
{
    bus.sent.assign((const char *)data, len);
    bus.sent_error = is_error;
}
void systemBus::addDevice(virtualDevice *pDevice, fujiDeviceID_t device_id) {}

#include "fnjson.h"

FNJSON::FNJSON() {}
FNJSON::~FNJSON() {}
void FNJSON::setLineEnding(const std::string &_lineEnding) {}
void FNJSON::setProtocol(NetworkProtocol *newProtocol) {}
void FNJSON::setReadQuery(const std::string &queryString, uint8_t queryParam) {}
bool FNJSON::readValue(uint8_t *buf, unsigned short len) { return false; }
bool FNJSON::parse() { return false; }

#include "fnhtml.h"

FNHTML::FNHTML() {}
FNHTML::~FNHTML() {}
void FNHTML::setLineEnding(const std::string &_lineEnding) {}
void FNHTML::setProtocol(NetworkProtocol *newProtocol) {}
void FNHTML::setReadQuery(const std::string &queryString, uint8_t queryParam) {}
bool FNHTML::readValue(uint8_t *buf, unsigned short len) { return false; }
bool FNHTML::parse() { return false; }

#include "fnxml.h"

FNXML::FNXML() {}
FNXML::~FNXML() {}
void FNXML::setLineEnding(const std::string &_lineEnding) {}
void FNXML::setProtocol(NetworkProtocol *newProtocol) {}
void FNXML::setReadQuery(const std::string &queryString, uint8_t queryParam) {}
bool FNXML::readValue(uint8_t *buf, unsigned short len) { return false; }
bool FNXML::parse() { return false; }

void util_debug_printf(const char *fmt, ...) {}
std::string util_hexdump(const void *buf, size_t len) { return ""; }
std::string util_remove_n_prefix(std::string url) { return url; }
std::string util_get_canonical_path(std::string path) { return ""; }
std::string util_devicespec_fix_for_parsing(std::string deviceSpec, std::string prefix, bool is_directory_read, bool process_fs_dot) { return ""; }
void util_devicespec_fix_9b(uint8_t* buf, unsigned short len) {}

std::unique_ptr<PeoplesUrlParser> PeoplesUrlParser::parseURL(const std::string &u) { return nullptr; }
bool PeoplesUrlParser::isValidUrl() { return false; }

#include "NetworkProtocolFactory.h"

std::unique_ptr<NetworkProtocol> NetworkProtocolFactory::createProtocol(std::string scheme, std::string *receiveBuffer, std::string *transmitBuffer, std::string *specialBuffer, std::string *login, std::string *password) { return nullptr; }

NetworkProtocol::NetworkProtocol(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
{}

NetworkProtocol::~NetworkProtocol() {}

fujiError_t NetworkProtocol::open(PeoplesUrlParser *urlParser, fileAccessMode_t access, netProtoTranslation_t translate)
{
    (void)urlParser; (void)access; (void)translate;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocol::close() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocol::read(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocol::status(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
void NetworkProtocol::errno_to_error() {}

// --------------------------- NetworkProtocolFS ------------------------------

NetworkProtocolFS::NetworkProtocolFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
}

NetworkProtocolFS::~NetworkProtocolFS() {}

fujiError_t NetworkProtocolFS::open(PeoplesUrlParser *urlParser, fileAccessMode_t access, netProtoTranslation_t translate)
{
    (void)urlParser; (void)access; (void)translate;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolFS::close() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::read(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::write(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::status(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::rename(PeoplesUrlParser *url) { (void)url; return FUJI_ERROR::NONE; }
size_t NetworkProtocolFS::available() { return 0; }

fujiError_t NetworkProtocolFS::open_file() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::open_dir(dirFormat_t fmt) { (void)fmt; return FUJI_ERROR::NONE; }
void NetworkProtocolFS::resolve() {}
fujiError_t NetworkProtocolFS::read_file(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::read_dir(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::status_file(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::status_dir(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::close_file() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::close_dir() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolFS::write_file(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
void NetworkProtocolFS::set_open_params(fileAccessMode_t access, netProtoTranslation_t translate) { (void)access; (void)translate; }

NetworkProtocolTCP::NetworkProtocolTCP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{}
NetworkProtocolTCP::~NetworkProtocolTCP() {}
fujiError_t NetworkProtocolTCP::accept_connection() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::close_client_connection() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::open(PeoplesUrlParser *urlParser, fileAccessMode_t access, netProtoTranslation_t translate) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::close() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::read(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::write(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolTCP::status(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
size_t NetworkProtocolTCP::available() { return 0; }

fnTcpClient::~fnTcpClient() {}

NetworkProtocolHTTP::NetworkProtocolHTTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{}
NetworkProtocolHTTP::~NetworkProtocolHTTP() {}
fujiError_t NetworkProtocolHTTP::set_channel_mode(netProtoHTTPChannelMode_t newMode) { return FUJI_ERROR::NONE; }
off_t NetworkProtocolHTTP::seek(off_t offset, int whence) { return 0; }
size_t NetworkProtocolHTTP::available() { return 0; }
fujiError_t NetworkProtocolHTTP::rename(PeoplesUrlParser *url) { (void)url; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::del(PeoplesUrlParser *url) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::mkdir(PeoplesUrlParser *url) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::rmdir(PeoplesUrlParser *url) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::open_file_handle() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::open_dir_handle() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::mount(PeoplesUrlParser *url) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::umount() { return FUJI_ERROR::NONE; }
void NetworkProtocolHTTP::fserror_to_error() {}
fujiError_t NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::status_file(NetworkStatus *status) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::close_file_handle() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::close_dir_handle() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolHTTP::stat() { return FUJI_ERROR::NONE; }

NetworkProtocolUDP::NetworkProtocolUDP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{}
NetworkProtocolUDP::~NetworkProtocolUDP() {}
fujiError_t NetworkProtocolUDP::open(PeoplesUrlParser *urlParser, fileAccessMode_t access, netProtoTranslation_t translate) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::close() { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::read(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::write(unsigned short len) { (void)len; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::status(NetworkStatus *status) { (void)status; return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::get_remote(void *sp_buf, unsigned short len) { return FUJI_ERROR::NONE; }
fujiError_t NetworkProtocolUDP::set_destination(const uint8_t *sp_buf, unsigned short len) { return FUJI_ERROR::NONE; }

fnUDP::fnUDP() {}
fnUDP::~fnUDP() {}
int fnUDP::available() { return 0; }

uint16_t FujiSIOPacket::getParam(size_t index, size_t psize) const
{
    if (psize == 1)
        return index ? frame.aux2 : frame.aux1;
    return frame.aux1 | frame.aux2 << 8;
}

void DaisyChain::addDevice(virtualDevice *newDev, fujiDeviceID_t fujiID) {}
void DaisyChain::assignFujiIDToDevice(virtualDevice *device, fujiDeviceID_t fujiID) {}

#include "NParser.h"
#include "JSONParser.h"
#include "HTMLParser.h"
#include "XMLParser.h"

fujiError_t NParser::read(std::string &buffer, size_t length) { return FUJI_ERROR::UNSPECIFIED; }
fujiError_t NParser::write(std::string &buffer) { return FUJI_ERROR::UNSPECIFIED; }
size_t NParser::available() { return 0; }
off_t NParser::seek(off_t offset, int whence) { return -1; }
error_is_true NParser::setQuery(const std::string &query) { RETURN_ERROR_AS_TRUE(); }
error_is_true NParser::parse() { RETURN_ERROR_AS_TRUE(); }
NetworkStatus NParser::status() { return {}; }
error_is_true NParser::setQueryParam(uint8_t param) { RETURN_ERROR_AS_TRUE(); }
error_is_true NParser::setLineEnding(const std::string &eol) { RETURN_ERROR_AS_TRUE(); }
fujiError_t JSONParser::write(std::string &buffer) { return FUJI_ERROR::UNSPECIFIED; }
off_t JSONParser::seek(off_t offset, int whence) { return -1; }
error_is_true JSONParser::setQuery(const std::string &query) { RETURN_ERROR_AS_TRUE(); }
error_is_true JSONParser::parse() { RETURN_ERROR_AS_TRUE(); }
error_is_true JSONParser::setQueryParam(uint8_t param) { RETURN_ERROR_AS_TRUE(); }
error_is_true JSONParser::setLineEnding(const std::string &eol) { RETURN_ERROR_AS_TRUE(); }
fujiError_t HTMLParser::write(std::string &buffer) { return FUJI_ERROR::UNSPECIFIED; }
off_t HTMLParser::seek(off_t offset, int whence) { return -1; }
error_is_true HTMLParser::setQuery(const std::string &query) { RETURN_ERROR_AS_TRUE(); }
error_is_true HTMLParser::parse() { RETURN_ERROR_AS_TRUE(); }
error_is_true HTMLParser::setQueryParam(uint8_t param) { RETURN_ERROR_AS_TRUE(); }
error_is_true HTMLParser::setLineEnding(const std::string &eol) { RETURN_ERROR_AS_TRUE(); }
fujiError_t XMLParser::write(std::string &buffer) { return FUJI_ERROR::UNSPECIFIED; }
off_t XMLParser::seek(off_t offset, int whence) { return -1; }
error_is_true XMLParser::setQuery(const std::string &query) { RETURN_ERROR_AS_TRUE(); }
error_is_true XMLParser::parse() { RETURN_ERROR_AS_TRUE(); }
error_is_true XMLParser::setQueryParam(uint8_t param) { RETURN_ERROR_AS_TRUE(); }
error_is_true XMLParser::setLineEnding(const std::string &eol) { RETURN_ERROR_AS_TRUE(); }
