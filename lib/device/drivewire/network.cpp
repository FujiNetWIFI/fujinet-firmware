#ifdef BUILD_COCO

/**
 * Network Firmware
 */

#include "network.h"
#include "../network.h"
#include "fuji_endian.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"

#include "status_error_codes.h"
#include "Protocol.h"
#include "TCP.h"
#include "UDP.h"
#include "HTTP.h"
#include "FS.h"

using namespace std;

/**
 * Constructor
 */
drivewireNetwork::drivewireNetwork()
{
    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
}

/**
 * Destructor
 */
drivewireNetwork::~drivewireNetwork()
{
    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    delete receiveBuffer;
    delete transmitBuffer;
    delete specialBuffer;
    receiveBuffer = nullptr;
    transmitBuffer = nullptr;
    specialBuffer = nullptr;

    if (protocol != nullptr)
        delete protocol;

    protocol = nullptr;
}

/** DRIVEWIRE COMMANDS ***************************************************************/

/**
 * DRIVEWIRE Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void drivewireNetwork::open(fileAccessMode_t access, netProtoTranslation_t trans_mode)
{
    Debug_printf("drivewireNetwork::open(%02x,%02x)\n", access, trans_mode);

    char tmp[256];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    if (SYSTEM_BUS.transaction_get(tmp, sizeof(tmp)).is_error())
    {
        Debug_printf("Short read. Exiting.");
        SYSTEM_BUS.transaction_error();
        return;
    }

    tmp[sizeof(tmp)-1] = '\0';
    Debug_printf("tmp = %s\n",tmp);

    deviceSpec = std::string(tmp);

    channelMode = PROTOCOL;

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        delete protocol;
        protocol = nullptr;
    }

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // Parse and instantiate protocol
    parse_and_instantiate_protocol(access == ACCESS_MODE::DIRECTORY);

    if (protocol == nullptr)
    {
        // invalid devicespec error already passed in.
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Set line ending to CR
    protocol->setLineEnding("\x0D");

    // Attempt protocol open
    if (protocol->open(urlParser.get(), access, trans_mode) != FUJI_ERROR::NONE)
    {
        _errorCode = protocol->error;
        Debug_printf("Protocol unable to make connection. Error: %d\n", _errorCode);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        SYSTEM_BUS.transaction_error();
        return;
    }

    // TODO: Finally, go ahead and let the parsers know
    json = new FNJSON();
    json->setLineEnding("\x0a");
    json->setProtocol(protocol);
    channelMode = PROTOCOL;

    SYSTEM_BUS.transaction_success();
}

/**
 * DRIVEWIRE Close command
 * Tear down everything set up by drivewire_open(), as well as RX interrupt.
 */
void drivewireNetwork::close()
{
    Debug_printf("drivewireNetwork::close()\n");

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        return;
    }

    // Ask the protocol to close
    protocol->close();

#ifdef ESP_PLATFORM
    Debug_printv("Before protocol delete %lu\n",esp_get_free_internal_heap_size());
#endif
    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    if (json != nullptr)
    {
        delete json;
        json = nullptr;
    }

#ifdef ESP_PLATFORM
    Debug_printv("After protocol delete %lu\n",esp_get_free_internal_heap_size());
#endif

    SYSTEM_BUS.transaction_success();
}

/**
 * DRIVEWIRE Read command
 * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
 * fill the rest with nulls and return ERROR.
 *
 * @note It is the channel's responsibility to pad to required length.
 */
void drivewireNetwork::read(uint16_t num_bytes)
{
    readAck = GET_TIMESTAMP();

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    if (!num_bytes)
    {
        Debug_printf("drivewireNetwork::read() - Zero bytes requested. Bailing.\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_printf("drivewireNetwork::read( %u bytes)\n", num_bytes);

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        _errorCode = NDEV_STATUS::COULD_NOT_ALLOCATE_BUFFERS;
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }

        SYSTEM_BUS.transaction_error();
        _errorCode = NDEV_STATUS::NOT_CONNECTED;
        return;
    }

    // Do the channel read
    read_channel(num_bytes);

    SYSTEM_BUS.transaction_send(*receiveBuffer);

    // Remove from receive buffer and shrink.
    receiveBuffer->erase(0, num_bytes);
    receiveBuffer->shrink_to_fit();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
fujiError_t drivewireNetwork::read_channel_json(unsigned short num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining = 0;
    else
        json_bytes_remaining -= num_bytes;

    return FUJI_ERROR::NONE;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
 */
fujiError_t drivewireNetwork::read_channel(unsigned short num_bytes)
{
    fujiError_t err = FUJI_ERROR::NONE;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->read(num_bytes);
        break;
    case JSON:
        err = read_channel_json(num_bytes);
        break;
    }
    return err;
}

/**
 * DRIVEWIRE Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to DRIVEWIRE. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void drivewireNetwork::write(uint16_t num_bytes)
{
    char *txbuf=nullptr;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (!num_bytes)
    {
        Debug_printf("drivewireNetwork::write() - refusing to write 0 bytes.\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    txbuf=(char *)malloc(num_bytes);

    if (!txbuf)
    {
        Debug_printf("drivewireNetwork::write() - could not allocate %u bytes.\n", num_bytes);
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (SYSTEM_BUS.transaction_get(txbuf, num_bytes).is_error())
    {
        Debug_printf("drivewireNetwork::write() - short read\n");
        free(txbuf);
        SYSTEM_BUS.transaction_error();
        return;
    }

    Debug_printf("drivewireNetwork::drivewire_write( %u bytes)\n", num_bytes);

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        SYSTEM_BUS.transaction_error();
        _errorCode = NDEV_STATUS::NOT_CONNECTED;
        return;
    }

    std::string s = std::string(txbuf,num_bytes);

    *transmitBuffer += s;

    free(txbuf);

    // Do the channel write
    write_channel(num_bytes);
    SYSTEM_BUS.transaction_success();
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit drivewire_error or drivewire_complete().
 */
fujiError_t drivewireNetwork::write_channel(unsigned short num_bytes)
{
    fujiError_t err = FUJI_ERROR::NONE;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->write(num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        err = FUJI_ERROR::UNSPECIFIED;
        break;
    }
    return err;
}

/**
 * DRIVEWIRE Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to DRIVEWIRE.
 */
void drivewireNetwork::status(uint8_t mode)
{
    if (protocol == nullptr)
        status_local(mode);
    else
        status_channel();
}

/**
 * @brief perform local status commands, if protocol is not bound, based on cmdFrame
 * value.
 */
void drivewireNetwork::status_local(uint8_t req)
{
    uint8_t ipAddress[4];
    uint8_t ipNetmask[4];
    uint8_t ipGateway[4];
    uint8_t ipDNS[4];
    NDeviceStatus status {};


#ifdef TOO_MUCH_DEBUG
    Debug_printf("drivewireNetwork::status_local(%u)\n", stat);
#endif /* TOO_MUCH_DEBUG */

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (req)
    {
    case 1: // IP Address
        Debug_printf("IP Address: %u.%u.%u.%u\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
        memcpy(&status,ipAddress,sizeof(status));
        break;
    case 2: // Netmask
        Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
        memcpy(&status,ipNetmask,sizeof(status));
        break;
    case 3: // Gatway
        Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
        memcpy(&status,ipGateway,sizeof(status));
        break;
    case 4: // DNS
        Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
        memcpy(&status,ipDNS,sizeof(status));
        break;
    default:
        status.conn = true;
        status.err = _errorCode;
        break;
    }

    SYSTEM_BUS.transaction_send(&status, sizeof(status));
}

bool drivewireNetwork::status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? NDEV_STATUS::SUCCESS : NDEV_STATUS::END_OF_FILE;
    return false; // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void drivewireNetwork::status_channel()
{
    NetworkStatus ns;
    NDeviceStatus status;
    size_t avail = 0;

#ifdef TOO_MUCH_DEBUG
    Debug_printf("drivewireNetwork::status_channel(%u)\n", channelMode);
#endif /* TOO_MUCH_DEBUG */

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status_channel on a null protocol.\r\n");
            ns.error = NDEV_STATUS::GENERAL;
        } else {
            protocol->status(&ns);
            avail = protocol->available();
        }
        break;
    case JSON:
        status_channel_json(&ns);
        avail = json_bytes_remaining;
        break;
    }

    avail = avail > 65535 ? 65535 : avail;
    status.avail = htobe16(avail);
    status.conn = ns.connected;
    status.err = ns.error;

#if 1 //def TOO_MUCH_DEBUG
    Debug_printf("status_channel() - BW: %u C: %u E: %u\n",
                 avail, ns.connected, ns.error);
#endif /* TOO_MUCH_DEBUG */

    SYSTEM_BUS.transaction_send(&status, sizeof(status));
}

/**
 * Get Prefix
 */
void drivewireNetwork::get_prefix()
{
    char out[256];
    Debug_printf("drivewireNetwork::get_prefix(%s)\n",prefix.c_str());
    memset(out,0,sizeof(out));
    strcpy(out,prefix.c_str());
    SYSTEM_BUS.transaction_send(out, sizeof(out));
}

/**
 * Set Prefix
 */
void drivewireNetwork::set_prefix()
{
    std::string prefixSpec_str;
    char tmp[256];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (SYSTEM_BUS.transaction_get(tmp, sizeof(tmp)).is_error())
    {
        Debug_printf("Short read. Exiting.");
        return;
    }

    prefixSpec_str = string((const char *)tmp);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("drivewireNetwork::set_prefix(%s)\n", prefixSpec_str.c_str());

    // If "NCD Nn:" then prefix is cleared completely
    if (prefixSpec_str.empty())
    {
        prefix.clear();
    }
    else
    {
        // For the remaining cases, append trailing slash if not found
        if (prefix[prefix.size()-1] != '/')
        {
            prefix += "/";
        }

        // Find pos of 3rd "/" in prefix
        size_t pos = prefix.find("/");
        pos = prefix.find("/",++pos);
        pos = prefix.find("/",++pos);

        // If "NCD Nn:.."" or "NCD .." then devance prefix
        if (prefixSpec_str == ".." || prefixSpec_str == "<")
        {
            prefix += ".."; // call to canonical path later will resolve
        }
        // If "NCD Nn:/" or "NCD /" then truncate to hostname (e.g. TNFS://hostname/)
        else if (prefixSpec_str == "/" || prefixSpec_str == ">")
        {
            // truncate at pos of 3rd slash
            prefix = prefix.substr(0,pos+1);
        }
        // If "NCD Nn:/path/to/dir/" then concatenate hostname and prefix
        else if (prefixSpec_str[0] == '/') // N:/DIR
        {
            // append at pos of 3rd slash
            prefix = prefix.substr(0,pos);
            prefix += prefixSpec_str;
        }
        // If "NCD TNFS://foo.com/" then reset entire prefix
        else if (prefixSpec_str.find_first_of(":") != string::npos)
        {
            prefix = prefixSpec_str;
            // Check for trailing slash. Append if missing.
            if (prefix[prefix.size()-1] != '/')
            {
                prefix += "/";
            }
        }
        else // append to path.
        {
            prefix += prefixSpec_str;
        }
    }

    prefix = util_get_canonical_path(prefix);
    Debug_printf("Prefix now: %s\n", prefix.c_str());

    SYSTEM_BUS.transaction_success();
}

/**
 * @brief set channel mode
 */
void drivewireNetwork::set_channel_mode(uint8_t mode)
{
    switch (mode)
    {
    case 0:
        channelMode = PROTOCOL;
        break;
    case 1:
        channelMode = JSON;
        break;
    default:
        break;
    }

    Debug_printv("channel mode now %u\n",channelMode);
}

/**
 * Set login
 */
void drivewireNetwork::set_login()
{
    char tmp[256];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (SYSTEM_BUS.transaction_get(tmp, sizeof(tmp)).is_error())
    {
        Debug_printf("Short read. Exiting.\n");
        return;
    }

    login = std::string(tmp,256);
    SYSTEM_BUS.transaction_success();

    Debug_printf("drivewireNetwork::set_login(%s)\n",login.c_str());
}

/**
 * Set password
 */
void drivewireNetwork::set_password()
{
    char tmp[256];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (SYSTEM_BUS.transaction_get(tmp, sizeof(tmp)).is_error())
    {
        Debug_printf("Short read. Exiting.\n");
        return;
    }

    password = std::string(tmp,256);
    SYSTEM_BUS.transaction_success();

    Debug_printf("drivewireNetwork::set_password(%s)\n", password.c_str());
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool drivewireNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("drivewireNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    if (!login.empty())
    {
        protocol->login = &login;
        protocol->password = &password;
    }

    Debug_printf("drivewireNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed (continue toggling PROCEED).
 */
bool drivewireNetwork::poll_interrupt()
{
    if (!protocol)
        return false;
    uint32_t now = GET_TIMESTAMP();
    if (now - readAck < 5000)
        return false;
    return protocol->available() > 0;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void drivewireNetwork::create_devicespec(bool is_dir)
{
    // Get Devicespec from buffer, and put into primary devicespec string

    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix, is_dir, true);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void drivewireNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void drivewireNetwork::parse_and_instantiate_protocol(bool is_dir)
{
    create_devicespec(is_dir);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        _errorCode = NDEV_STATUS::INVALID_DEVICESPEC;
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
        _errorCode = NDEV_STATUS::GENERAL;
        return;
    }
}

/**
 * Is this a valid URL? (Used to generate ERROR 165)
 */
bool drivewireNetwork::isValidURL(PeoplesUrlParser *url)
{
    if (url->scheme == "")
        return false;
    else if ((url->path == "") && (url->port == ""))
        return false;
    else
        return true;
}

void drivewireNetwork::parse_json()
{
    bool success = json->parse();

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    _errorCode = NDEV_STATUS::SUCCESS;
#ifdef UNUSED
    // Atari doesn't check for errors and blindly returns that
    // everything is fine. This causes the httpbin test to pass when
    // it probably shouldn't. However we'll just do what Atari does.
    if (!success)
        _errorCode = NDEV_STATUS::COULD_NOT_PARSE_JSON;
#endif /* UNUSED */
    SYSTEM_BUS.transaction_success();
}

void drivewireNetwork::json_query()
{
    std::string in_string;
    char tmpq[256];

    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);

    if (SYSTEM_BUS.transaction_get(tmpq, sizeof(tmpq)).is_error())
    {
        Debug_printf("Short read. Exiting\n");
        SYSTEM_BUS.transaction_error();
        return;
    }

    in_string = std::string(tmpq);

    // strip away line endings from input spec.
    for (int i = 0; i < in_string.size(); i++)
    {
        unsigned char currentChar = static_cast<unsigned char>(in_string[i]);
        if (currentChar == 0x0A || currentChar == 0x0D || currentChar == 0x9b)
        {
            in_string.resize(i);
            break;
        }
    }

    // Query param is only used in ATARI at the moment, and 256 is too large for the type.
    json->setReadQuery(in_string, 0);
    json_bytes_remaining = json->available();

    std::vector<uint8_t> tmp(json_bytes_remaining);
    json->readValue(tmp.data(), json_bytes_remaining);

    // don't copy past first nul char in tmp
    auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    *receiveBuffer += std::string(tmp.begin(), null_pos);

#if 0
    for (int i=0;i<in_string.length();i++)
        Debug_printf("%02X ",(unsigned char)in_string[i]);

    Debug_printf("\n");
#endif

    Debug_printf("Query set to >%s<\r\n", in_string.c_str());
    SYSTEM_BUS.transaction_success();
}

void drivewireNetwork::processCommand(FujiDWPacket &packet)
{
    Debug_printf("comnd: '%c' %u\n", packet.command(), packet.command());

    switch (packet.command())
    {
    case NETCMD_OPEN:
        open(static_cast<fileAccessMode_t>(packet.param8(0)),
             static_cast<netProtoTranslation_t>(packet.param8(1)));
        break;
    case NETCMD_CLOSE:
        close();
        break;
    case NETCMD_READ:
        read(packet.param(0));
        break;
    case NETCMD_WRITE:
        write(packet.param(0));
        break;
    case NETCMD_STATUS:
        status(packet.param(1));
        break;

    case NETCMD_PARSE:
        parse_json();
        break;
    case NETCMD_CHANNEL_MODE:
        set_channel_mode(packet.param(0));
        break;

    case NETCMD_GETCWD:
        get_prefix();
        break;

    case NETCMD_CHDIR:
        set_prefix();
        return;
    case NETCMD_QUERY:
        json_query();
        return;
    case NETCMD_USERNAME:
        set_login();
        return;
    case NETCMD_PASSWORD:
        set_password();
        return;

    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        process_fs(packet);
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp(packet);
        break;

    case NETCMD_SET_CHANNEL_MODE:
        process_http(packet);
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp(packet);
        break;

    default:
        SYSTEM_BUS.transaction_error();
        break;
    }
}

void drivewireNetwork::process_fs(FujiDWPacket &packet)
{
    parse_and_instantiate_protocol(static_cast<fileAccessMode_t>(packet.param8(0))
                                   == ACCESS_MODE::DIRECTORY);

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol);
    if (!fs)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t err;
    auto url = urlParser.get();
    switch (packet.command())
    {
    case NETCMD_RENAME:
        err = fs->rename(url);
        break;
    case NETCMD_DELETE:
        err = fs->del(url);
        break;
    case NETCMD_LOCK:
        err = fs->lock(url);
        break;
    case NETCMD_UNLOCK:
        err = fs->unlock(url);
        break;
    case NETCMD_MKDIR:
        err = fs->mkdir(url);
        break;
    case NETCMD_RMDIR:
        err = fs->rmdir(url);
        break;
    default:
        err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (err != FUJI_ERROR::NONE)
    {
        SYSTEM_BUS.transaction_error();
    }
}

void drivewireNetwork::process_tcp(FujiDWPacket &packet)
{
    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol);
    if (!tcp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t err;
    switch (packet.command())
    {
    case NETCMD_CONTROL:
        err = tcp->accept_connection();
        break;
    case NETCMD_CLOSE_CLIENT:
        err = tcp->close_client_connection();
        break;
    default:
        err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (err != FUJI_ERROR::NONE)
    {
        SYSTEM_BUS.transaction_error();
    }
}

void drivewireNetwork::process_http(FujiDWPacket &packet)
{
    // Make sure this is really an HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol);
    if (!http)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    fujiError_t err;
    switch (packet.command())
    {
    case NETCMD_SET_CHANNEL_MODE:
        err = http->set_channel_mode((netProtoHTTPChannelMode_t) packet.param8(1));
        break;
    default:
        err = FUJI_ERROR::UNSPECIFIED;
        return;
    }

    if (err != FUJI_ERROR::NONE)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

void drivewireNetwork::process_udp(FujiDWPacket &packet)
{
    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol);
    if (!udp)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t err;
    switch (packet.command())
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        receiveBuffer->resize(SPECIAL_BUFFER_SIZE);
        err = udp->get_remote(receiveBuffer->data(), receiveBuffer->size());
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(*receiveBuffer);
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            uint8_t spData[SPECIAL_BUFFER_SIZE];
            SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
            SYSTEM_BUS.transaction_get(spData, sizeof(spData));
            err = udp->set_destination(spData, sizeof(spData));
            if (err != FUJI_ERROR::NONE)
            {
                SYSTEM_BUS.transaction_error();
                return;
            }
        }
        break;
    default:
        SYSTEM_BUS.transaction_error();
        return;
    }

    SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_COCO */
