#ifdef BUILD_LYNX

/**
 * N: Firmware
 */

#include "lynxNetwork.h"
#include "../network.h"
#include "NetworkProtocolFactory.h"
#include "utils.h"
#include "debug.h"

using namespace std;

/**
 * Constructor
 */
lynxNetwork::lynxNetwork()
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
lynxNetwork::~lynxNetwork()
{
    // delete protocol instance
    if (protocol != nullptr)
        protocol.reset();
    protocol = nullptr;

    // delete all buffers
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

/** LYNX COMMANDS ***************************************************************/

/**
 * LYNX Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void lynxNetwork::open(const FujiLynxPacket &packet)
{
    fileAccessMode_t open_mode = (fileAccessMode_t) packet.param8(0);
    netProtoTranslation_t open_trans = (netProtoTranslation_t) packet.param8(1);


    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        protocol.reset();
    }

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("lynxNetwork::open - aux1:%02X aux2:%02X %s\n",
                 (unsigned int) open_mode, (unsigned int) open_trans,
                 packet.dataAsString()->c_str());

    // Parse and instantiate protocol
    bool is_dir = static_cast<fileAccessMode_t>(packet.param8(0)) == ACCESS_MODE::DIRECTORY;
    parse_and_instantiate_protocol(*packet.dataAsString(), is_dir);

    if (protocol == nullptr)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), open_mode, open_trans) != FUJI_ERROR::NONE)
    {
        statusByte.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", (int)err);
        protocol.reset();
        SYSTEM_BUS.transaction_error();
        return;
    }

    // setup JSON
    json = new FNJSON();
    json->setLineEnding("\x00");        // null terminate json values always
    json->setProtocol(protocol.get());
    channelMode = PROTOCOL;

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

/**
 * LYNX Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void lynxNetwork::close()
{
    Debug_printf("lynxNetwork::close\n");

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    statusByte.byte = 0x00;

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        SYSTEM_BUS.transaction_success();
        return;
    }

    // Ask the protocol to close
    protocol->close();

    // Delete the protocol object
    protocol.reset();

    // delete the json object
    if (json != nullptr)
    {
        delete json;
        json = nullptr;
    }

    SYSTEM_BUS.transaction_success();
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to bus_to_computer().
 */
fujiError_t lynxNetwork::read_channel(unsigned short num_bytes)
{
    fujiError_t _err = FUJI_ERROR::NONE;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    switch (channelMode)
    {
    case PROTOCOL:
        read_channel_protocol();
        break;
    case JSON:
        {
            std::string buffer(json->available() % SERIAL_PACKET_SIZE, 0);
            json->readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
            Debug_printf("lynxNetwork:receive_channel_json, len:%d %s\n",
                         buffer.size(), buffer.c_str());
            SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
            SYSTEM_BUS.transaction_send(buffer);
            _err = FUJI_ERROR::NONE;
        }
        break;
    default:
        Debug_println("lynxNetwork::read_channel - unknown channelMode");
        SYSTEM_BUS.transaction_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return _err;
}

/**
 * LYNX Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to LYNX. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void lynxNetwork::write(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    Debug_printf("lynxNetwork::write\n");

    *transmitBuffer += *packet.dataAsString();
    err = write_channel(packet.dataAsString()->size()) == FUJI_ERROR::NONE ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
}


void lynxNetwork::read()
{
    Debug_println("lynxNetwork::read");
    read_channel();
}



/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit comlynx_error or comlynx_complete().
 */
fujiError_t lynxNetwork::write_channel(unsigned short num_bytes)
{
    fujiError_t err = FUJI_ERROR::NONE;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

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

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
    return err;
}

/**
 * LYNX Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to LYNX.
 */
void lynxNetwork::status()
{
    NetworkStatus s;
    //NDeviceStatus *status = (NDeviceStatus *) response;
    NDeviceStatus status;

    Debug_println("lynxNetwork::status");

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = s.error = NDEV_STATUS::NOT_CONNECTED;
            SYSTEM_BUS.transaction_error();
        } else {
            err = protocol->status(&s) == FUJI_ERROR::NONE ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
        }
        break;
    case JSON:
        // err = json->status(&status);
        break;
    }

    size_t avail = protocol->available();
    avail = avail > 65535 ? 65535 : avail;
    status.avail = avail;
    status.conn = s.connected;
    status.err = s.error;

    Debug_printf("lynxNetwork::comlynx_status - avail:%d conn:%d err:%d\n", status.avail, status.conn, (int)status.err);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(&status, sizeof(status));
}

/**
 * Get Prefix
 */
void lynxNetwork::get_prefix()
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    Debug_printf("lynxNetwork::comlynx_getprefix(%s)\n", prefix.c_str());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(prefix);
}

/**
 * Set Prefix
 */
void lynxNetwork::set_prefix(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    std::string prefixSpec_str = *packet.dataAsString();
    Debug_printf("lynxNetwork::comlynx_set_prefix(%s)\n", prefixSpec_str.c_str());

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
void lynxNetwork::set_login(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    login = *packet.dataAsString();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

/**
 * Set password
 */
void lynxNetwork::set_password(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    password = *packet.dataAsString();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void lynxNetwork::set_channel_mode(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    Debug_printf("lynxNetwork::channel_mode - mode: %02X\n", packet.param8(0));

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);

    switch (packet.param8(0))
    {
    case 0:
        channelMode = PROTOCOL;
        SYSTEM_BUS.transaction_success();
        break;
    case 1:
        channelMode = JSON;
        SYSTEM_BUS.transaction_success();
        break;
    default:
        SYSTEM_BUS.transaction_error();
        break;
    }
}

void lynxNetwork::json_query(const FujiLynxPacket &packet)
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    // read the json value from query, there may be more bytes than we can transfer
    // in one response
    uint8_t val = packet.param(1);
    // get the query string
    Debug_printf("lynxNetwork::json_query - query:%s\n", packet.dataAsString()->c_str());
    json->setReadQuery(*packet.dataAsString(), val);
    uint16_t jsonlen = json->available();
    jsonlen = std::min<uint16_t>(json->available(), SERIAL_PACKET_SIZE);
    Debug_printf("lynxNetwork::json_query - json->available:%d, len:%d\n", json->available(), jsonlen);

    if (jsonlen == 0) {
      SYSTEM_BUS.transaction_error();
      return;
    }

    std::vector<uint8_t> tmp(jsonlen);
    json->readValue(tmp.data(), jsonlen);

    // don't copy past first nul char in tmp (don't think this is needed -SJ)
    //auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    //*receiveBuffer += std::string(tmp.begin(), null_pos);

    Debug_printf("lynxNetwork::json_query - value:%.*s\n", static_cast<int>(jsonlen), reinterpret_cast<const char*>(tmp.data()));
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(tmp);
}

void lynxNetwork::json_parse()
{
  if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return; // Punch out.
    }

    Debug_println("lynxNetwork::json_parse");
    json->parse();
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_success();
}

void lynxNetwork::read_channel()
{
    switch (channelMode)
    {
    case JSON:
        read_channel_json();
        break;
    case PROTOCOL:
        read_channel_protocol();
        break;
    }
}

void lynxNetwork::read_channel_json()
{
    //NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // check how many bytes available and truncated to packet size
    size_t len = std::min<uint16_t>(json->available(), SERIAL_PACKET_SIZE);

    if (len == 0) {
      SYSTEM_BUS.transaction_error();
      return;
    }

    std::string buffer(len, 0);
    json->readValue(reinterpret_cast<uint8_t *>(buffer.data()), buffer.size());
    Debug_printf("lynxNetwork:read_channel_json, len:%d %s\n", buffer.size(), buffer.c_str());
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(buffer);
}

void lynxNetwork::read_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Get status
    protocol->status(&ns);
    size_t avail = protocol->available();

    Debug_printf("lynxNetwork:read_channel_protocol - protcol->available:%d\n", avail);

    if (!avail)
    {
        SYSTEM_BUS.transaction_error();
        return;
    }

    // Truncate bytes waiting to response size
    avail = std::min<uint16_t>(avail, SERIAL_PACKET_SIZE);

    if (protocol->read(avail) != FUJI_ERROR::NONE) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        SYSTEM_BUS.transaction_error();
        return;
    }

    statusByte.bits.client_error = 0;
    statusByte.bits.client_data_available = avail > 0;
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(receiveBuffer->data(), avail);
    receiveBuffer->erase(0, avail);
}

/**
 * Process incoming LYNX command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void lynxNetwork::comlynx_process(const FujiLynxPacket &packet)
{
    Debug_printf("lynxNetwork::comlynx_process - command: %02X\n", (uint8_t) packet.command());

    switch (packet.command())
    {
    case CMD::NET_CHDIR:
        set_prefix(packet);
        break;
    case CMD::NET_GETCWD:
        get_prefix();
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
    case CMD::NET_READ:
        read();
        break;
    case CMD::NET_WRITE:
        write(packet);
        break;
    case CMD::NET_CHANNEL_MODE:
        set_channel_mode(packet);
        break;
    case CMD::NET_PARSE:
    case CMD::NET_PARSE_ALT:
        json_parse();
        break;
    case CMD::NET_QUERY:
    case CMD::NET_QUERY_ALT:
        json_query(packet);
        break;
    case CMD::NET_USERNAME: // login
        set_login(packet);
        break;
    case CMD::NET_PASSWORD: // password
        set_password(packet);
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
        Debug_printf("lynxnetwork::comlynx_process - unknown command: %02X", (uint8_t) packet.command());
        SYSTEM_BUS.transaction_error();
        break;
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool lynxNetwork::instantiate_protocol()
{
    protocol = NetworkProtocolFactory::createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("lynxNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("lynxNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void lynxNetwork::create_devicespec(string d, bool is_dir)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, is_dir, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void lynxNetwork::create_url_parser()
{
    urlParser = PeoplesUrlParser::parseURL(deviceSpec);
}

void lynxNetwork::parse_and_instantiate_protocol(string d, bool is_dir)
{
    create_devicespec(d, is_dir);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NDEV_STATUS::INVALID_DEVICESPEC;
        return;
    }
#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
#endif
    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NDEV_STATUS::GENERAL;
        return;
    }
}

/*void lynxNetwork::comlynx_set_translation()
{
}*/


void lynxNetwork::process_fs(const FujiLynxPacket &packet)
{
    statusByte.byte = 0x00;

    bool is_dir = static_cast<fileAccessMode_t>(packet.param8(0)) == ACCESS_MODE::DIRECTORY;
    parse_and_instantiate_protocol(*packet.dataAsString(), is_dir);

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol.get());
    if (!fs)
    {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
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

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (cmd_err != FUJI_ERROR::NONE) {
        SYSTEM_BUS.transaction_error();
        statusByte.bits.client_error = true;
    }
    else
        SYSTEM_BUS.transaction_success();
}

void lynxNetwork::process_tcp(const FujiLynxPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol.get());
    if (!tcp)
    {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
    case CMD::NET_CONTROL:
        cmd_err = FUJI_ERROR::NONE;

        // Because we're not handling Adam bus very well, sometimes it
        // retries and we've already accepted which will return an
        // error. Don't do accept if client is already connected.
        // LYNX NOT LIKE ADAM BUS ANY LONGER -SJ
        {
            NetworkStatus status;
            tcp->status(&status);
            if (!status.connected)
            {
                cmd_err = tcp->accept_connection();
                Debug_printf("ACCEPT %x CHANMODE %d ERR: %d\n", (uint8_t) id(), channelMode, (int)cmd_err);
            }
        }
        break;
    case CMD::NET_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (cmd_err != FUJI_ERROR::NONE) {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
    }
    else
        SYSTEM_BUS.transaction_success();
}

void lynxNetwork::process_http(const FujiLynxPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol.get());
    if (!http)
    {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
    case CMD::NET_SET_CHANNEL_MODE:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) packet.param8(1));
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        return;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (cmd_err != FUJI_ERROR::NONE) {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
    }
    else
        SYSTEM_BUS.transaction_success();
}

void lynxNetwork::process_udp(const FujiLynxPacket &packet)
{
    statusByte.byte = 0x00;

    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol.get());
    if (!udp)
    {
        statusByte.bits.client_error = true;
        SYSTEM_BUS.transaction_error();
        return;
    }

    fujiError_t cmd_err;
    switch (packet.command())
    {
#ifndef ESP_PLATFORM
    case CMD::NET_GET_REMOTE:
        receiveBuffer->resize(SPECIAL_BUFFER_SIZE);
        cmd_err = udp->get_remote(receiveBuffer->data(), receiveBuffer->size());
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(*receiveBuffer);
        break;
#endif /* ESP_PLATFORM */
    case CMD::NET_SET_DESTINATION:
        {
            uint8_t spData[SPECIAL_BUFFER_SIZE];
            size_t bytes_read = SYSTEM_BUS.read(spData, sizeof(spData));
            cmd_err = udp->set_destination(spData, bytes_read);
            if (cmd_err != FUJI_ERROR::NONE)
                statusByte.bits.client_error = true;
        }
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        statusByte.bits.client_error = true;
        break;
    }

    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    if (cmd_err != FUJI_ERROR::NONE)
        SYSTEM_BUS.transaction_error();
    else
        SYSTEM_BUS.transaction_success();
}

#endif /* BUILD_LYNX */
