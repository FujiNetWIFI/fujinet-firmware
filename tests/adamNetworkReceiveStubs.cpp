// Link-time stand-ins for ADAM network dependencies; bus replies are recorded in `bus`.

#include "adamnet.h"
#include "NDevice.h"
#include "BoIPChannel.h"
#include "adamNetworkReceiveStubs.h"

systemBus SYSTEM_BUS;
BusLog bus;

BoIPChannel::BoIPChannel() : _ip(0), _port(0), _listening(false), _local_echo(false),
    _non_blocking(false), _no_delay(false), _fd(-1), _listen_fd(-1), _state(BoIPStopped) {}
BoIPChannel::~BoIPChannel() = default;
void BoIPChannel::updateFIFO() {}
size_t BoIPChannel::dataOut(const void *, size_t) { return 0; }
void BoIPChannel::end() {}
void BoIPChannel::flushOutput() {}

void virtualDevice::reset() {}
void virtualDevice::adamnet_control_ready() {}
void virtualDevice::adamnet_control_receive() {}
void virtualDevice::adamnet_idle() {}

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

void systemBus::transaction_accept(transState_t expectMoreData) {}
void systemBus::transaction_success() { bus.success++; }
void systemBus::transaction_error() { bus.error++; }
success_is_true systemBus::transaction_get(void *data, size_t len) { RETURN_ERROR_AS_FALSE(); }
void systemBus::transaction_send(const void *data, size_t len, bool is_error)
{
    bus.sent.assign((const char *)data, len);
    (void)is_error;
}

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

void PeoplesUrlParser::resetURL(const std::string u) {}
bool PeoplesUrlParser::isValidUrl() { return false; }
std::string PeoplesUrlParser::getFilePath(bool decode) const { return ""; }

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
