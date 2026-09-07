#ifdef BUILD_ADAM

/**
 * N: Firmware
 */

#include "adamNetwork.h"
#include "../network.h"
#include "NetworkProtocolFactory.h"
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
    // First the protocol: ~NetworkProtocol() still touches the buffers below.
    protocol.reset();

    // Then the buffers we own.
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
        json.setProtocol(nullptr);
        sgml.setProtocol(nullptr);
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
        json.setProtocol(nullptr);
        sgml.setProtocol(nullptr);
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

    // Ask the protocol to close. Latch its error so the STATUS that follows a
    // failed commit-on-close (e.g. a calendar compose) can still report it.
    if (protocol->close() != FUJI_ERROR::NONE)
        err_open = protocol->error;

    // Delete the protocol object
    protocol.reset();

    // Don't leave json/sgml holding a dangling protocol pointer
    json.setProtocol(nullptr);
    sgml.setProtocol(nullptr);
}

/**
 * ADAM Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to ADAM. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void adamNetwork::write(const FujiAdamPacket &packet)
{
    // Nothing open to write to, e.g. a WRITE following a failed OPEN.
    if ((protocol == nullptr) || (transmitBuffer == nullptr))
    {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    auto data = packet.dataAsString().value();
    Debug_printf("adamNetwork::write(%u)\n", (unsigned) data.size());
    *transmitBuffer += data;

    // Surface a protocol failure via status(), not a bus NAK.
    if (adamnet_write_channel(data.size()) != FUJI_ERROR::NONE)
        statusByte.bits.client_error = true;

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

    // Caller owns the bus reply, so just report the error here.
    if (protocol == nullptr)
        return FUJI_ERROR::UNSPECIFIED;

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
    NDeviceStatus status{};

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (protocol == nullptr)
    {
        // No protocol: report the reason the last open failed.
        status.avail = 0;
        status.conn = 0;
        status.err = err_open;
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
    case CMD::NET_CHDIR:
        set_prefix(packet);
        break;
    case CMD::NET_GETCWD:
        get_prefix();
        break;
    case CMD::NET_GET_ERROR:
        get_error();
        break;
    case CMD::NET_OPEN:
        open(packet);
        break;
    case CMD::NET_CLOSE:
        close();
        break;
    case CMD::NET_STATUS:
        status();
        break;
    case CMD::NET_WRITE:
        write(packet);
        break;
    case CMD::NET_CHANNEL_MODE:
        channel_mode(packet);
        break;
    case CMD::NET_USERNAME: // login
        set_login(packet);
        break;
    case CMD::NET_PASSWORD: // password
        set_password(packet);
        break;

    case CMD::NET_PARSE:
        if (channelMode == CHANNEL_MODE::SGML)
            sgml_parse();
        else
            json_parse();
        break;
    case CMD::NET_QUERY:
        if (channelMode == CHANNEL_MODE::SGML)
            sgml_query(packet);
        else
            json_query(packet);
        break;

    case CMD::NET_RENAME:
    case CMD::NET_DELETE:
    case CMD::NET_LOCK:
    case CMD::NET_UNLOCK:
    case CMD::NET_MKDIR:
    case CMD::NET_RMDIR:
        process_fs(packet);
        break;

    case CMD::NET_CONTROL:
    case CMD::NET_CLOSE_CLIENT:
        process_tcp(packet);
        break;

    case CMD::NET_SET_CHANNEL_MODE:
        process_http(packet);
        break;

    case CMD::NET_GET_REMOTE:
    case CMD::NET_SET_DESTINATION:
        process_udp(packet);
        break;

    default:
        statusByte.bits.client_error = true;
        break;
    }
}

std::optional<ByteBuffer> adamNetwork::adamnet_control_receive_channel_json()
{
    NetworkStatus ns;

    auto len = json.readValueLen();
    ByteBuffer buffer(len);
    json.readValue(buffer.data(), buffer.size());
    return buffer;
}

std::optional<ByteBuffer> adamNetwork::adamnet_control_receive_channel_sgml()
{
    NetworkStatus ns;

    auto len = sgml.readValueLen();
    ByteBuffer buffer(len);
    sgml.readValue(buffer.data(), buffer.size());
    return buffer;
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
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        if (buffer->size())
            SYSTEM_BUS.transaction_send(buffer.value());
        else
            SYSTEM_BUS.transaction_success();
    }
    else
    {
        Debug_printf("No data\n");
        SYSTEM_BUS.transaction_error();
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool adamNetwork::instantiate_protocol()
{
    protocol = NetworkProtocolFactory::createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

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
    urlParser = PeoplesUrlParser::parseURL(deviceSpec);
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
    case CMD::NET_RENAME:
        cmd_err = fs->rename(url);
        break;
    case CMD::NET_DELETE:
        cmd_err = fs->del(url);
        break;
    case CMD::NET_LOCK:
        cmd_err = fs->lock(url);
        break;
    case CMD::NET_UNLOCK:
        cmd_err = fs->unlock(url);
        break;
    case CMD::NET_MKDIR:
        cmd_err = fs->mkdir(url);
        break;
    case CMD::NET_RMDIR:
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
    case CMD::NET_CONTROL:
        cmd_err = FUJI_ERROR::NONE;

        {
            cmd_err = tcp->accept_connection();
            Debug_printf("ACCEPT %x CHANMODE %d ERR: %d\n", id(), channelMode, cmd_err);

            // Because we're not handling Adam bus very well, sometimes it
            // retries and we've already accepted which will return an
            // error. Clear error if client is already connected.
            cmd_err = FUJI_ERROR::NONE;
            statusByte.bits.client_error = false;
        }
        break;
    case CMD::NET_CLOSE_CLIENT:
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
    case CMD::NET_SET_CHANNEL_MODE:
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
    case CMD::NET_GET_REMOTE:
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
    case CMD::NET_SET_DESTINATION:
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
