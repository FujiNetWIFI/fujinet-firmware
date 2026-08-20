#ifdef BUILD_ADAM

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"
#include "ProtocolParser.h"
#include "utils.h"
#include "debug.h"

#define MAX_ADAM_PACKET_LEN 1024

using namespace std;

/**
 * Constructor
 */
adamNetwork::adamNetwork()
{
#ifndef ESP_PLATFORM
    // BoIP: N: ops block on a remote host; send past the 300us window.
    _pc_no_response_deadline = true;
#endif

    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    protocol = nullptr;

    json.setLineEnding("\x00");
    sgml.setLineEnding("\x00");
}

/**
 */
adamNetwork::~adamNetwork()
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
        protocol.reset();

    protocol = nullptr;
}

/** ADAM COMMANDS ***************************************************************/

/**
 * @brief get error number from protocol adapter
 */
void adamNetwork::get_error()
{
    NetworkStatus ns;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    Debug_printf("Get Error\n");

    if (protocol == nullptr)
    {
        SYSTEM_BUS.transaction_send((int) err_open);
    }
    else
    {
        // passive, like the bus status poll
        protocol->fromInterrupt = true;
        protocol->status(&ns);
        protocol->fromInterrupt = false;
        SYSTEM_BUS.transaction_send((int) ns.error);
    }
}
/**
 * ADAM Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void adamNetwork::open(const FujiAdamPacket &packet)
{
    fileAccessMode_t mode = static_cast<fileAccessMode_t>(packet.param8(0));
    netProtoTranslation_t trans = static_cast<netProtoTranslation_t>(packet.param8(1));

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        protocol.reset();
    }

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("open()\n");

    // Parse and instantiate protocol
    bool is_dir = (fileAccessMode_t) packet.param8(0) == ACCESS_MODE::DIRECTORY;
    parse_and_instantiate_protocol(packet.dataAsString().value(), is_dir);

    if (protocol == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Set the human-readable line ending for this platform (Adam: CR).
    protocol->setLineEnding("\x0d");

    // Narrow DIR_FORMAT::LONG entries to fit Adam's 32-column mode.
    protocol->setDirLongWidth(30);

    // Attempt protocol open
    if (protocol->open(urlParser.get(), mode, trans) != FUJI_ERROR::NONE)
    {
        statusByte.bits.client_error = true;
        err_open = protocol->error; // keep the reason for get_error()
        Debug_printf("Protocol unable to make connection.\n");
        protocol.reset();
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Associate channel mode
    json.setProtocol(protocol.get());
    sgml.setProtocol(protocol.get());

    // Clear response
    Debug_printf("::open() complete err=%d\n", statusByte.bits.client_error);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

/**
 * ADAM Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void adamNetwork::close()
{
    Debug_printf("adamNetwork::close()\n");

    statusByte.byte = 0x00;
    err_open = NDEV_STATUS::NOT_CONNECTED;

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        return;
    }

    // Ask the protocol to close
    protocol->close();

    // Delete the protocol object
    protocol.reset();
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
 */
fujiError_t adamNetwork::read_channel(unsigned short num_bytes)
{
    fujiError_t _err = FUJI_ERROR::NONE;

    switch (channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        _err = protocol->read(num_bytes);
        break;
    case CHANNEL_MODE::JSON:
        Debug_printf("JSON Not Handled.\n");
        _err = FUJI_ERROR::UNSPECIFIED;
        break;
    case CHANNEL_MODE::SGML:
        Debug_printf("SGML Not Handled.\n");
        _err = FUJI_ERROR::UNSPECIFIED;
        break;
    }
    return _err;
}

/**
 * ADAM Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to ADAM. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void adamNetwork::write(const FujiAdamPacket &packet)
{
    Debug_printf("!!! WRITE\n");
    auto data = packet.dataAsString().value();
    *transmitBuffer += data;
    adamnet_write_channel(data.size());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit adamnet_error or adamnet_complete().
 */
fujiError_t adamNetwork::adamnet_write_channel(unsigned short num_bytes)
{
    fujiError_t err_net = FUJI_ERROR::NONE;

    switch (channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        err_net = protocol->write(num_bytes);
        break;
    case CHANNEL_MODE::JSON:
        Debug_printf("JSON Not Handled.\n");
        err_net = FUJI_ERROR::UNSPECIFIED;
        break;
    case CHANNEL_MODE::SGML:
        Debug_printf("SGML Not Handled.\n");
        err_net = FUJI_ERROR::UNSPECIFIED;
        break;
    }
    return err_net;
}

/**
 * ADAM Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to ADAM.
 */
void adamNetwork::status()
{
    NetworkStatus ns;
    NDeviceStatus status;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (protocol == nullptr)
    {
        status.avail = 0;
        status.conn = 0;
        status.err = err_open;
        return;
    }
    else
    {
        switch (channelMode)
        {
        case CHANNEL_MODE::PROTOCOL:
            protocol->status(&ns);
            break;
        case CHANNEL_MODE::JSON:
            // err = json.status(&status);
            break;
        case CHANNEL_MODE::SGML:
            // err = sgml.status(&status);
            break;
        }

        size_t avail = protocol->available();
        avail = avail > 65535 ? 65535 : avail;
        status.avail = avail;
        status.conn = ns.connected;
        status.err = ns.error;
    }

    SYSTEM_BUS.transaction_send(&status, sizeof(status));

    Debug_printf("status() - BW: %u C: %u E: %u\n", status.avail, status.conn, status.err);
}

/**
 * Get Prefix
 */
void adamNetwork::get_prefix()
{
    Debug_printf("adamNetwork::adamnet_getprefix(%s)\n", prefix.c_str());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(prefix);
}

/**
 * Set Prefix
 */
void adamNetwork::set_prefix(const FujiAdamPacket &packet)
{
    auto prefixSpec_str = packet.dataAsString().value();
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("adamNetwork::adamnet_set_prefix(%s)\n", prefixSpec_str.c_str());

    if (prefixSpec_str == "..") // Devance path N:..
    {
        std::vector<int> pathLocations;
        for (int i = 0; i < prefix.size(); i++)
        {
            if (prefix[i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (prefix[prefix.size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        prefix = prefix.substr(0, pathLocations.back() + 1);
    }
    else if (prefixSpec_str[0] == '/') // N:/DIR
    {
        prefix = prefixSpec_str;
    }
    else if (prefixSpec_str.empty())
    {
        prefix.clear();
    }
    else if (prefixSpec_str.find_first_of(":") != string::npos)
    {
        prefix = prefixSpec_str;
    }
    else // append to path.
    {
        prefix += prefixSpec_str;
    }

    Debug_printf("Prefix now: %s\n", prefix.c_str());

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

/**
 * Set login
 */
void adamNetwork::set_login(const FujiAdamPacket &packet)
{
    login = packet.dataAsString().value();
}

/**
 * Set password
 */
void adamNetwork::set_password(const FujiAdamPacket &packet)
{
    password = packet.dataAsString().value();
}

void adamNetwork::channel_mode(const FujiAdamPacket &packet)
{
    channelMode_t mode = static_cast<channelMode_t>(packet.param8(0));
    switch (mode)
    {
    case CHANNEL_MODE::PROTOCOL:
    case CHANNEL_MODE::JSON:
    case CHANNEL_MODE::SGML:
        channelMode = mode;
        break;
    default:
        SYSTEM_BUS.transaction_error();
        return;
    }
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void adamNetwork::json_query(const FujiAdamPacket &packet)
{
    std::string query = packet.dataAsString().value();
    json.setReadQuery(query, 0);
    Debug_printv("adamNetwork::json_query(%s)\n", query.c_str());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void adamNetwork::json_parse()
{
    bool ok = json.parse();
    if (protocol != nullptr)
        protocol->error = ok ? NDEV_STATUS::SUCCESS : NDEV_STATUS::COULD_NOT_PARSE_JSON;
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void adamNetwork::sgml_query(const FujiAdamPacket &packet)
{
    std::string query = packet.dataAsString().value();
    sgml.setReadQuery(query, 0);
    Debug_printv("adamNetwork::json_query(%s)\n", query.c_str());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void adamNetwork::sgml_parse()
{
    bool ok = sgml.parse();
    if (protocol != nullptr)
        protocol->error = ok ? NDEV_STATUS::SUCCESS : NDEV_STATUS::COULD_NOT_PARSE_JSON;
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

AdamNetStatus adamNetwork::deviceStatus()
{
    AdamNetStatus status;
    NetworkStatus s;

    if (protocol != nullptr)
    {
        // passive: a bus status poll must not trigger deferred protocol
        // work (e.g. the lazy HTTP transaction) inside the reply deadline
        protocol->fromInterrupt = true;
        protocol->status(&s);
        protocol->fromInterrupt = false;
        statusByte.bits.client_connected = s.connected == true;
        statusByte.bits.client_data_available = protocol->available() > 0;
        statusByte.bits.client_error = s.error != NDEV_STATUS::SUCCESS;
    }

    status.length = MAX_ADAM_PACKET_LEN;
    status.devtype = ADAMNET_DEVTYPE::CHAR;
    status.status = statusByte.byte;

    return status;
}

void adamNetwork::adamnet_control_send(const FujiAdamPacket &packet)
{
    Debug_printf("Network command 0x%02x\n", packet.command());
    switch (packet.command())
    {
    case NETCMD_CHDIR:
        set_prefix(packet);
        break;
    case NETCMD_GETCWD:
        get_prefix();
        break;
    case NETCMD_GET_ERROR:
        get_error();
        break;
    case NETCMD_OPEN:
        open(packet);
        break;
    case NETCMD_CLOSE:
        close();
        break;
    case NETCMD_STATUS:
        status();
        break;
    case NETCMD_WRITE:
        write(packet);
        break;
    case NETCMD_CHANNEL_MODE:
        channel_mode(packet);
        break;
    case NETCMD_USERNAME: // login
        set_login(packet);
        break;
    case NETCMD_PASSWORD: // password
        set_password(packet);
        break;

    case NETCMD_PARSE:
        if (channelMode == CHANNEL_MODE::SGML)
            sgml_parse();
        else
            json_parse();
        break;
    case NETCMD_QUERY:
        if (channelMode == CHANNEL_MODE::SGML)
            sgml_query(packet);
        else
            json_query(packet);
        break;

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
        statusByte.bits.client_error = true;
        break;
    }
}

void adamNetwork::adamnet_control_clr()
{
    adamnet_response_send();

    if (channelMode == CHANNEL_MODE::JSON)
        jsonRecvd = false;
    else if (channelMode == CHANNEL_MODE::SGML)
        sgmlRecvd = false;
}

std::optional<ByteBuffer> adamNetwork::adamnet_control_receive_channel_json()
{
    NetworkStatus ns;

    if (jsonRecvd == false)
    {
        auto len = json.readValueLen();
        ByteBuffer buffer(len);
        json.readValue(buffer.data(), buffer.size());
        jsonRecvd = true;
        return buffer;
    }
    return std::nullopt;
}

std::optional<ByteBuffer> adamNetwork::adamnet_control_receive_channel_sgml()
{
    NetworkStatus ns;

    if (sgmlRecvd == false)
    {
        auto len = sgml.readValueLen();
        ByteBuffer buffer(len);
        sgml.readValue(buffer.data(), buffer.size());
        sgmlRecvd = true;
        return buffer;
    }
    return std::nullopt;
}

std::optional<ByteBuffer> adamNetwork::adamnet_control_receive_channel_protocol()
{
    NetworkStatus ns;

    // Get status
    protocol->status(&ns);
    if (ns.error != NDEV_STATUS::SUCCESS)
        return std::nullopt;

    size_t avail = protocol->available();
    avail = std::min<size_t>(MAX_ADAM_PACKET_LEN, avail);
    if (!avail)
        return ByteBuffer();

    if (protocol->read(avail) != FUJI_ERROR::NONE) // protocol adapter returned error
        return std::nullopt;

    statusByte.bits.client_error = 0;
    statusByte.bits.client_data_available = avail > 0;
    ByteBuffer buffer;
    buffer.assign(receiveBuffer->data(), receiveBuffer->data() + avail);
    receiveBuffer->erase(0, avail);
    return buffer;
}

inline void adamNetwork::adamnet_control_receive()
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr))
    {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    std::optional<ByteBuffer> buffer;
    switch (channelMode)
    {
    case CHANNEL_MODE::JSON:
        buffer = adamnet_control_receive_channel_json();
        break;
    case CHANNEL_MODE::SGML:
        buffer = adamnet_control_receive_channel_sgml();
        break;
    case CHANNEL_MODE::PROTOCOL:
        buffer = adamnet_control_receive_channel_protocol();
        break;
    default:
        Debug_printf("INVALID CHANNEL MODE %d\n", channelMode);
        SYSTEM_BUS.transaction_error();
        return;
    }

    if (buffer.has_value())
    {
        if (buffer->size())
        {
            SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
            SYSTEM_BUS.transaction_send(buffer.value());
        }
        else
            SYSTEM_BUS.stall_silent = true;
    }
    else
    {
        Debug_printf("No data\n");
        SYSTEM_BUS.transaction_error();
    }
}

void adamNetwork::adamnet_response_send()
{
    if (response_len)
    {
        // response_len can be a full 1024 bytes; on the half-duplex bus that whole
        // burst echoes back into our own RX and, at rxThreshold=1, storms the shared
        // UART ISR enough to jitter the outgoing bytes. Coalesce the echo for the
        // duration of the send (same fix as the disk block stream).
        SYSTEM_BUS.quiet_rx_for_send(true);
        FujiAdamPacket packet(_devnum, APT::NM_SEND,
                              ByteBuffer(response, response + response_len));
        auto encoded = packet.serialize();
        adamnet_send_buffer(encoded.data(), encoded.size());
        response_len = 0;
        SYSTEM_BUS.quiet_rx_for_send(false);
    }
    else
        adamnet_send(0xC0 | _devnum); // NAK!

    memset(response, 0, response_len);
    response_len = 0;
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool adamNetwork::instantiate_protocol()
{
    protocol = ProtocolParser::createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("adamNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("adamNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void adamNetwork::create_devicespec(string d, bool is_dir)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, is_dir, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
 */
void adamNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void adamNetwork::parse_and_instantiate_protocol(string d, bool is_dir)
{
    create_devicespec(d, is_dir);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: %s\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err_open = NDEV_STATUS::INVALID_DEVICESPEC;
        return;
    }

    Debug_printf("::parse_and_instantiate_protocol transformed to (%s, %s)\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol.\n");
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err_open = NDEV_STATUS::INVALID_DEVICESPEC; // unknown scheme
        return;
    }
}

void adamNetwork::adamnet_set_translation()
{
    // trans_aux2 = cmdFrame.aux2;
    // adamnet_complete();
}

void adamNetwork::adamnet_set_timer_rate()
{
    // timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // // Stop extant timer
    // timer_stop();

    // // Restart timer if we're running a protocol.
    // if (protocol != nullptr)
    //     timer_start();

    // adamnet_complete();
}

void adamNetwork::process_fs(const FujiAdamPacket &packet)
{
    parse_and_instantiate_protocol(packet.dataAsString().value(), false);

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol.get());
    if (!fs)
    {
        statusByte.bits.client_error = true;
        return;
    }

    fujiError_t cmd_err;
    auto url = urlParser.get();
    switch (packet.command())
    {
    case NETCMD_RENAME:
        cmd_err = fs->rename(url);
        break;
    case NETCMD_DELETE:
        cmd_err = fs->del(url);
        break;
    case NETCMD_LOCK:
        cmd_err = fs->lock(url);
        break;
    case NETCMD_UNLOCK:
        cmd_err = fs->unlock(url);
        break;
    case NETCMD_MKDIR:
        cmd_err = fs->mkdir(url);
        break;
    case NETCMD_RMDIR:
        cmd_err = fs->rmdir(url);
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void adamNetwork::process_tcp(const FujiAdamPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol.get());
    if (!tcp)
    {
        statusByte.bits.client_error = true;
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
    case NETCMD_CONTROL:
        cmd_err = FUJI_ERROR::NONE;

        {
            cmd_err = tcp->accept_connection();
            Debug_printf("ACCEPT %x CHANMODE %d ERR: %d\n", _devnum, channelMode, cmd_err);

            // Because we're not handling Adam bus very well, sometimes it
            // retries and we've already accepted which will return an
            // error. Clear error if client is already connected.
            cmd_err = FUJI_ERROR::NONE;
            statusByte.bits.client_error = false;
        }
        break;
    case NETCMD_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void adamNetwork::process_http(const FujiAdamPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol.get());
    if (!http)
    {
        statusByte.bits.client_error = true;
        if (protocol != nullptr)
            protocol->error = NDEV_STATUS::INVALID_COMMAND;
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
    case NETCMD_SET_CHANNEL_MODE:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) packet.param8(0));
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        return;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void adamNetwork::process_udp(const FujiAdamPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol.get());
    if (!udp)
    {
        statusByte.bits.client_error = true;
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
    {
        receiveBuffer->resize(SPECIAL_BUFFER_SIZE);
        cmd_err = udp->get_remote(receiveBuffer->data(), receiveBuffer->size());
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        size_t len = std::min<size_t>(MAX_ADAM_PACKET_LEN, receiveBuffer->size());
        SYSTEM_BUS.transaction_send(receiveBuffer->data(), len);
        receiveBuffer->erase(0, len);
        break;
    }
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            cmd_err = udp->set_destination(packet.data()->data(), packet.data()->size());
            if (cmd_err != FUJI_ERROR::NONE)
                statusByte.bits.client_error = true;
        }
        break;
    default:
        statusByte.bits.client_error = true;
        break;
    }
}

#endif /* BUILD_ADAM */
