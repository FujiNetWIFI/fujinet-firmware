/**
 * NetworkDeviceBase implementation.
 *
 * Compiled once per firmware target, same as every other .cpp in this tree --
 * add this file to whichever BUILD_xxx target you're building. It resolves
 * "network.h" the same way each xxx/network.cpp already resolves its own
 * "../network.h": via the include path for that target, which is expected to
 * put the relevant subdirectory first so NetworkPacket is defined before this
 * file needs it.
 */

#include "NDevice.h"
#include "NetworkProtocolFactory.h"
#include "NParser.h"
#include "JSONParser.h"
#include "SGMLParser.h"
#include "IOChannel.h" // For GET_TIMESTAMP()
#include "utils.h"
#include "debug.h"

#include <type_traits>

#define IS_DIR_MODE(access_mode)                                            \
    ({fileAccessMode_t _am = static_cast<fileAccessMode_t>(access_mode);    \
        _am == ACCESS_MODE::DIRECTORY || _am == ACCESS_MODE::DIRECTORY_ALT;})

const std::unordered_map<fujiCommandID_t, NDevice::Handler> NDevice::dispatch_table = {
    { CMD::NET_OPEN,            {&NDevice::fujidev_open}                  },
    { CMD::NET_CLOSE,           {&NDevice::fujidev_close}                 },
    { CMD::NET_READ,            {&NDevice::fujidev_read}                  },
    { CMD::NET_WRITE,           {&NDevice::fujidev_write}                 },
    { CMD::NET_STATUS,          {&NDevice::fujidev_status}                },

    { CMD::NET_PARSE,           {&NDevice::fujidev_do_parse}              },
    { CMD::NET_QUERY,           {&NDevice::fujidev_set_query}             },
    { CMD::NET_SET_PARAMETER,   {&NDevice::fujidev_set_parameter}        },
    { CMD::NET_SET_PARSER,      {&NDevice::fujidev_set_parser}            },
    { CMD::NET_TRANSLATION,     {&NDevice::fujidev_set_translation}       },
    { CMD::NET_SET_EOL,         {&NDevice::fujidev_set_eol}               },
    { CMD::NET_SET_INT_RATE,    {&NDevice::fujidev_set_timer_rate}        },
    { CMD::NET_SEEK,            {&NDevice::fujidev_seek}                  },
    { CMD::NET_TELL,            {&NDevice::fujidev_tell}                  },

    { CMD::NET_GETCWD,          {&NDevice::fujidev_get_prefix}            },
    { CMD::NET_CHDIR,           {&NDevice::fujidev_set_prefix}            },
    { CMD::NET_USERNAME,        {&NDevice::fujidev_set_login}             },
    { CMD::NET_PASSWORD,        {&NDevice::fujidev_set_password}          },

    { CMD::NET_RENAME,          {&NDevice::fujidev_rename}                },
    { CMD::NET_DELETE,          {&NDevice::fujidev_delete}                },
    { CMD::NET_LOCK,            {&NDevice::fujidev_lock}                  },
    { CMD::NET_UNLOCK,          {&NDevice::fujidev_unlock}                },
    { CMD::NET_MKDIR,           {&NDevice::fujidev_mkdir}                 },
    { CMD::NET_RMDIR,           {&NDevice::fujidev_rmdir}                 },

    { CMD::NET_ACCEPT,          {&NDevice::fujidev_tcp_accept}            },
    { CMD::NET_CLOSE_CLIENT,    {&NDevice::fujidev_tcp_close_client}      },

    { CMD::NET_SET_HTTP_MODE,   {&NDevice::fujidev_http_set_channel_mode} },

    { CMD::NET_GET_REMOTE,      {&NDevice::fujidev_udp_get_remote}        },
    { CMD::NET_SET_DESTINATION, {&NDevice::fujidev_udp_set_destination}   },
};

NDevice::NDevice()
{
    receiveBuffer = new std::string();
    transmitBuffer = new std::string();
    specialBuffer = new std::string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
}

NDevice::~NDevice()
{
    // _protocol & _parser are unique_ptr -- destroyed automatically.

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    delete receiveBuffer;
    delete transmitBuffer;
    delete specialBuffer;
    receiveBuffer = nullptr;
    transmitBuffer = nullptr;
    specialBuffer = nullptr;
}

bool NDevice::processCommand(const FUJI_COMMAND_PACKET &packet)
{
    {
        uint8_t cmd = (uint8_t) packet.command();
        Debug_printf("NDevice=0x%02x processCommand: '%c' 0x%02x\n",
                     (unsigned) packet.device(), isprint(cmd) ? cmd : '.', cmd);
    }

    auto it = dispatch_table.find(packet.command());
    if (it == dispatch_table.end())
    {
        Debug_printf("NDevice::process() - unknown command: %02X\n", (unsigned) packet.command());
        SYSTEM_BUS.transaction_error();
        return false;
    }

    (this->*(it->second))(packet);
    return true;
}

bool NDevice::recognizesCommand(fujiCommandID_t command)
{
    auto it = dispatch_table.find(command);
    if (it != dispatch_table.end())
        return true;
    return false;
}

// ============================= hook defaults ===============================

std::string NDevice::network_eol() const
{
    return network_eol_override.empty() ? SYSTEM_BUS.nativeEOL() : network_eol_override;
}

#ifdef HAVE_LAST_ERROR
NDeviceStatus NDevice::status_local(uint8_t mode)
{
    NDeviceStatus status;

    (void)mode;
    status.conn = false;
    status.err = lastError;
    status.avail = 0;
    return status;
}
#endif /* HAVE_LAST_ERROR */

// ============================ shared operations =============================

void NDevice::fujidev_open(const FUJI_COMMAND_PACKET &packet)
{
    fileAccessMode_t access = param_cast<fileAccessMode_t>(packet, 0);
    netProtoTranslation_t trans_mode = param_cast<netProtoTranslation_t>(packet, 1);

    std::string spec(256, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    if (SYSTEM_BUS.transaction_get(spec).is_error())
    {
        Debug_printf("Failed to get device spec.");
        SYSTEM_BUS.transaction_error();
        return;
    }
    spec.resize(strlen(spec.c_str()));
    spec = SYSTEM_BUS.nativeTextToUnicode(spec);

    // Shut down protocol if we are sending another open before we close.
    if (_protocol != nullptr)
        _protocol->close();
    _protocol = nullptr;
    _parser = nullptr;

    if (!parse_and_instantiate_protocol(spec, IS_DIR_MODE(access), urlParser))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (dir_long_width() > 0)
        _protocol->setDirLongWidth(dir_long_width());

    // Ignore trans_mode value if trans_override set 0xFF, for ACTION!
    if (static_cast<uint8_t>(trans_override) == 0xFF)
        trans_mode = NETPROTO_TRANS::NONE;
    else if (!IS_DIR_MODE(access))
    {
        unsigned flags = static_cast<unsigned>(trans_mode) | static_cast<unsigned>(trans_override);
        trans_mode = static_cast<netProtoTranslation_t>(flags);
    }

    if (_protocol->open(urlParser.get(), access, trans_mode) != FUJI_ERROR::NONE)
    {
#ifdef HAVE_LAST_ERROR
        lastError = protocol->error;
#endif /* HAVE_LAST_ERROR */
        Debug_printf("Protocol unable to make connection. Error: %d\n",
                     (unsigned) _protocol->error);
        _protocol = nullptr;
        _parser = nullptr;
        SYSTEM_BUS.transaction_error();
        return;
    }

    _parser = std::make_unique<NParser>(_protocol.get());

    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_close(const FUJI_COMMAND_PACKET &packet)
{
    (void)packet;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (_protocol != nullptr && _protocol->close() != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();

    _protocol = nullptr;
    _parser = nullptr;
}

error_is_true NDevice::fujicore_read(ByteBuffer &buf, size_t len)
{
    fujiError_t err;

    std::string strbuf;
    err = _parser->read(strbuf, len);
    if (err == FUJI_ERROR::NONE)
        buf.assign(strbuf.begin(), strbuf.end());
    RETURN_ERROR_IF(err != FUJI_ERROR::NONE);
}

void NDevice::fujidev_read(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    uint16_t num_bytes = packet.param(0);

    if (!num_bytes)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    ByteBuffer buf;
    if (fujicore_read(buf, num_bytes).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_send(buf);
}

error_is_true NDevice::fujicore_write(const ByteBuffer &buf)
{
    if (_parser == nullptr)
        RETURN_ERROR_AS_TRUE();

    fujiError_t err;
    std::string strbuf(buf.begin(), buf.end());
    err = _parser->write(strbuf);
    RETURN_ERROR_IF(err != FUJI_ERROR::NONE);
}

void NDevice::fujidev_write(const FUJI_COMMAND_PACKET &packet)
{
    uint16_t num_bytes = packet.param(0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (!num_bytes)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    ByteBuffer buf(num_bytes);
    if (SYSTEM_BUS.transaction_get(buf).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (_protocol == nullptr || transmitBuffer == nullptr)
    {
#ifdef HAVE_LAST_ERROR
        lastError = NDEV_STATUS::NOT_CONNECTED;
#endif /* HAVE_LAST_ERROR */
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (fujicore_write(buf).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

size_t NDevice::fujicore_available()
{
    return _parser->available();
}

NDeviceStatus NDevice::current_status()
{
    NDeviceStatus nstatus;

    if (_protocol == nullptr)
    {
#ifdef HAVE_LAST_ERROR
        return status_local(mode);
#else
        nstatus.avail = 0;
        nstatus.conn = 0;
        nstatus.err = NDEV_STATUS::NOT_CONNECTED;
#endif /* HAVE_LAST_ERROR */
    }
    else
    {
        NetworkStatus ns = _parser->status();

        nstatus.avail = std::min<size_t>(65535, fujicore_available());
        nstatus.conn = ns.connected;
        nstatus.err = ns.error;
    }

#if 0
    Debug_printf("NDevice::status avail=%d conn=%d err=%d\n",
                 nstatus.avail, nstatus.conn, nstatus.err);
#endif
    return nstatus;
}

NDeviceStatus NDevice::fujicore_status()
{
    readAck = GET_TIMESTAMP();
    return current_status();
}

void NDevice::fujidev_status(const FUJI_COMMAND_PACKET &packet)
{
    auto nstatus = fujicore_status();
    readAck = GET_TIMESTAMP();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(&nstatus, sizeof(nstatus), false);
}

void NDevice::fujidev_get_prefix(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send((uint8_t *)prefix.data(), prefix.size(), false);
}

error_is_true NDevice::fujicore_set_prefix(const std::string &pathSpec)
{
    std::string path = util_remove_n_prefix(pathSpec);
    Debug_printf("NDevice::set_prefix(%s)\n", path.c_str());

    if (path.empty()) // Nn: with nothing after it clears the prefix completely
    {
        prefix.clear();
        RETURN_SUCCESS_AS_FALSE();
    }

    // Append trailing slash if not found
    if (path.back() != '/')
    {
        path += "/";
    }

    if (!prefix.empty() && prefix.back() != '/')
        prefix += "/";

    // Position of the 3rd "/" in prefix (i.e. right after the host, for SCHEME://host/...).
    size_t pos = prefix.find("/");
    if (pos != std::string::npos)
    {
        pos = prefix.find("/", pos + 1);
        if (pos != std::string::npos)
            pos = prefix.find("/", pos + 1);
    }

    if (path == ".." || path == "<") // Devance path
    {
        prefix += "..";
    }
    else if (path == "/" || path == ">") // Truncate to host, e.g. TNFS://host/
    {
        if (pos != std::string::npos)
            prefix = prefix.substr(0, pos + 1);
    }
    else if (path[0] == '/') // Nn:/path/to/dir/ -- keep host, replace path
    {
        if (pos != std::string::npos)
            prefix = prefix.substr(0, pos);
        prefix += path;
    }
    else if (path.find_first_of(":") != std::string::npos) // SCHEME://host/... -- reset entirely
    {
        prefix = path;
        if (prefix.back() != '/')
            prefix += "/";
    }
    else // relative -- append to path
    {
        prefix += path;
    }

    prefix = util_get_canonical_path(prefix);
    Debug_printf("Prefix now: %s\n", prefix.c_str());

    RETURN_SUCCESS_AS_FALSE();
}

void NDevice::fujidev_set_prefix(const FUJI_COMMAND_PACKET &packet)
{
    std::string prefix(256, 0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(prefix);
    prefix.resize(strlen(prefix.c_str()));
    if (fujicore_set_prefix(prefix).is_error())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }
    SYSTEM_BUS.transaction_success();
}

error_is_true NDevice::fujicore_set_query(const std::string &query, uint8_t parseFlags)
{
    error_is_true err = error_is_true(false);
    err = _parser->setQuery(query);
    if (err.is_success())
        Debug_printf("Query set to >%s<\r\n", query.c_str());
    return err;
}

void NDevice::fujidev_set_query(const FUJI_COMMAND_PACKET &packet)
{
    uint8_t query_param = 0; //packet.param(1);

    std::string query(256, 0);

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(query);
    query.resize(strlen(query.c_str()));
    query = SYSTEM_BUS.nativeTextToUnicode(query);

    fujicore_set_query(query, query_param);
    SYSTEM_BUS.transaction_success();
}

bool NDevice::parse_and_instantiate_protocol(std::string &deviceSpec, bool is_dir,
                                             std::unique_ptr<PeoplesUrlParser> &url_out)
{
    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix, is_dir, true);
    url_out = PeoplesUrlParser::parseURL(deviceSpec);

    if (!url_out->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
#ifdef HAVE_LAST_ERROR
        lastError = NDEV_STATUS::INVALID_DEVICESPEC;
#endif /* HAVE_LAST_ERROR */
        _protocol = nullptr;
        _parser = nullptr;
        return false;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), url_out->mRawUrl.c_str());
#endif

    _protocol = NetworkProtocolFactory::createProtocol(url_out->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (_protocol == nullptr)
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), url_out->mRawUrl.c_str());
#ifdef HAVE_LAST_ERROR
        lastError = NDEV_STATUS::GENERAL;
#endif /* HAVE_LAST_ERROR */
        return false;
    }

    _protocol->native_eol = network_eol();

    Debug_printf("NDevice::parse_and_instantiate_protocol() - Protocol %s created.\n", url_out->scheme.c_str());
    return true;
}

void NDevice::fujidev_set_login(const FUJI_COMMAND_PACKET &packet)
{
    login.resize(256, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(login);
    login.resize(strlen(login.c_str()));
    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_set_password(const FUJI_COMMAND_PACKET &packet)
{
    password.resize(256);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(password);
    password.resize(strlen(password.c_str()));
    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_set_parser(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    parserMode_t mode = param_cast<parserMode_t>(packet, 1);
    switch (mode)
    {
    case PARSER::NONE:
        _parser = std::make_unique<NParser>(_protocol.get());
        break;

    case PARSER::JSON:
        _parser = std::make_unique<JSONParser>(_protocol.get());
        break;

    case PARSER::SGML:
        _parser = std::make_unique<SGMLParser>(_protocol.get());
        break;

    default:
        Debug_printf("INVALID MODE = %02x\r\n", (unsigned) mode);
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_do_parse(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    _parser->parse();
    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_set_eol(const FUJI_COMMAND_PACKET &packet)
{
    std::string eol;

    auto data = SYSTEM_BUS.transaction_varlen_string(packet);
    if (!data.has_value())
    {
        SYSTEM_BUS.transaction_error();
        return;
    }
    eol = data.value();

    network_eol_override.clear();
    if (!eol.empty()) {
        network_eol_override = eol;
    }
    if (_protocol != nullptr) {
        _protocol->native_eol = network_eol();
    }
    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_set_parameter(const FUJI_COMMAND_PACKET &packet)
{
    if (!_parser)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    // param(0) | param(1)  | meaning
    // 0        | 0/1/2     | Set the json->_queryParam value, which is the
    //                        translation value for string processing
    // 1        | c         | Set the json->lineEnding = c, convert from char to
    //                        single byte string

    parserParam_t ptype = param_cast<parserParam_t>(packet, 0);
    switch (ptype)
    {
    case PARSER_PARAM::QUERY:
        if (param_as<uint8_t>(packet, 1) > 2)
        {
            SYSTEM_BUS.transaction_error();
            return;
        }
        _parser->setQueryParam(packet.param(1));
        SYSTEM_BUS.transaction_success();
        break;
    case PARSER_PARAM::EOL:
        {
            std::stringstream ss;
            ss << param_as<uint8_t>(packet, 1);
            string new_le = ss.str();
            Debug_printf("JSON line ending changed to 0x%02hx\r\n",
                         param_as<uint8_t>(packet, 1));
            _parser->setLineEnding(new_le);
            SYSTEM_BUS.transaction_success();
            break;
        }
    default:
        SYSTEM_BUS.transaction_error();
        break;
    }
}

error_is_true NDevice::fujicore_seek(size_t offset)
{
    if (_parser == nullptr)
        RETURN_ERROR_AS_TRUE();

    if (_parser->seek(offset, SEEK_SET) == -1)
        RETURN_ERROR_AS_TRUE();

    if (_parser->seek(offset, SEEK_SET) == -1)
        RETURN_ERROR_AS_TRUE();

    RETURN_SUCCESS_AS_FALSE();
}

void NDevice::fujidev_seek(const FUJI_COMMAND_PACKET &packet)
{
    u32ne_t offset;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (fujicore_seek(param_as<uint32_t>(packet, 0)).is_error())
    {
#ifdef HAVE_LAST_ERROR
        lastError = NDEV_STATUS::INVALID_POINT;
#endif /* HAVE_LAST_ERROR */
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_tell(const FUJI_COMMAND_PACKET &packet)
{
    u32le_t offset;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (_parser != nullptr)
        offset = _parser->seek(0, SEEK_CUR);

    SYSTEM_BUS.transaction_send(&offset, sizeof(offset));
}

// ================================ fs ops ====================================

void NDevice::fs_op(const FUJI_COMMAND_PACKET &packet, fujiError_t (NetworkProtocolFS::*op)(PeoplesUrlParser *))
{
    uint8_t mode = packet.param(0);
    uint8_t unused = packet.param(1);

    std::string spec(256, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(spec);
    spec.resize(strlen(spec.c_str()));

    std::unique_ptr<PeoplesUrlParser> url;
    if (!parse_and_instantiate_protocol(spec, IS_DIR_MODE(mode), url))
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(_protocol.get());
    if (!fs)
    {
        SYSTEM_BUS.transaction_error();
        _protocol = nullptr;
        _parser = nullptr;
        return;
    }

    fujiError_t err = (fs->*op)(url.get());

    // This was a one-shot protocol just for this fs operation.
    _protocol = nullptr;
    _parser = nullptr;

    if (err != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_rename(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::rename);
}
void NDevice::fujidev_delete(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::del);
}
void NDevice::fujidev_lock(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::lock);
}
void NDevice::fujidev_unlock(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::unlock);
}
void NDevice::fujidev_mkdir(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::mkdir);
}
void NDevice::fujidev_rmdir(const FUJI_COMMAND_PACKET &packet) {
    fs_op(packet, &NetworkProtocolFS::rmdir);
}

// ================================ tcp ops ===================================

void NDevice::fujidev_tcp_accept(const FUJI_COMMAND_PACKET &packet)
{
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(_protocol.get());
    if (!tcp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (tcp->accept_connection() != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

void NDevice::fujidev_tcp_close_client(const FUJI_COMMAND_PACKET &packet)
{
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(_protocol.get());
    if (!tcp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (tcp->close_client_connection() != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

// ================================ http ops ==================================

void NDevice::fujidev_http_set_channel_mode(const FUJI_COMMAND_PACKET &packet)
{
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(_protocol.get());
    if (!http)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    auto mode = param_cast<netProtoHTTPChannelMode_t>(packet, 1);
    if (http->set_channel_mode(mode) != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

// ================================ udp ops ===================================

void NDevice::fujidev_udp_get_remote(const FUJI_COMMAND_PACKET &packet)
{
#ifndef ESP_PLATFORM
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(_protocol.get());
    if (!udp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    fujiError_t err = udp->get_remote(receiveBuffer->data(), SPECIAL_BUFFER_SIZE);
    SYSTEM_BUS.transaction_send((uint8_t *)receiveBuffer->data(), SPECIAL_BUFFER_SIZE, err != FUJI_ERROR::NONE);
#else
    SYSTEM_BUS.transaction_error();
#endif
}

void NDevice::fujidev_udp_set_destination(const FUJI_COMMAND_PACKET &packet)
{
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(_protocol.get());
    if (!udp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    auto dest = SYSTEM_BUS.transaction_varlen_data(packet);
    if (!dest.has_value()
        || udp->set_destination(dest->data(), dest->size()) != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed
 * (continue toggling PROCEED).
 */
bool NDevice::poll_interrupt()
{
    if (!_protocol)
        return false;
    uint64_t delta = GET_TIMESTAMP() - readAck;
    if (delta < 5000)
        return false;
    delta /= 1000; // micro to milli
    delta /= timerRate * 2;
    if (delta % 2)
        return false;
    bool hasUpdate = _parser->available() > 0;
    if (!hasUpdate)
    {
        nDevStatus_t err;
#ifdef HAVE_LAST_ERROR
        err = lastError;
#else
        _protocol->fromInterrupt = true;
        auto nstatus = fujicore_status();
        _protocol->fromInterrupt = false;
        if (!nstatus.conn)
            hasUpdate = true;
        err = nstatus.err;
#endif /* HAVE_LAST_ERROR */

        hasUpdate |= err != NDEV_STATUS::SUCCESS && err != NDEV_STATUS::END_OF_FILE;
    }

    return hasUpdate;
}

void NDevice::fujidev_set_timer_rate(const FUJI_COMMAND_PACKET &packet)
{
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    timerRate = param_as<uint8_t>(packet, 0);
    SYSTEM_BUS.transaction_success();
}
