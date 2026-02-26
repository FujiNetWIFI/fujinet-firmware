#ifdef BUILD_LYNX

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"

#include "utils.h"

#include "status_error_codes.h"
#include "ProtocolParser.h"
#include "TCP.h"
#include "UDP.h"
#include "HTTP.h"
#include "FS.h"

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

    json.setLineEnding("\x00");
}

/**
 * Destructor
 */
lynxNetwork::~lynxNetwork()
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

/** LYNX COMMANDS ***************************************************************/

/**
 * LYNX Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void lynxNetwork::open(unsigned short len)
{
    uint8_t _aux1;
    uint8_t _aux2;
    string d;


    transaction_get(&_aux1, sizeof(_aux1));
    transaction_get(&_aux2, sizeof(_aux2));

    memset(response, 0, sizeof(response));
    transaction_get(&response, len);

    channelMode = PROTOCOL;

    // persist aux1/aux2 values
    cmdFrame.aux1 = _aux1;
    cmdFrame.aux2 = _aux2;

    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        delete protocol;
        protocol = nullptr;

        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
    }

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("lynxNetwork::open - aux1: %02X aux2: %02X %s\n", open_aux1, open_aux2, response);

    // Parse and instantiate protocol
    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        transaction_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), (fileAccessMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2) != PROTOCOL_ERROR::NONE)
    {
        statusByte.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        transaction_error();
        return;
    }

    // Associate channel mode
    json.setProtocol(protocol);
    transaction_complete();
}

/**
 * LYNX Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void lynxNetwork::close()
{
    Debug_printf("lynxNetwork::close\n");

     statusByte.byte = 0x00;

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        transaction_complete();
        return;
    }

    // Ask the protocol to close
    protocol->close();

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    transaction_complete();
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return PROTOCOL_ERROR::UNSPECIFIED on error, PROTOCOL_ERROR::NONE on success. Passed directly to bus_to_computer().
 */
protocolError_t lynxNetwork::read_channel(unsigned short num_bytes)
{
    protocolError_t _err = PROTOCOL_ERROR::NONE;

    switch (channelMode)
    {
    case PROTOCOL:
        read_channel_protocol();
        break;
    case JSON:
        response_len = json.available();
        response_len = response_len % SERIAL_PACKET_SIZE;
        json.readValue(response, response_len);

        _err = PROTOCOL_ERROR::NONE;
        break;
    default:
        Debug_println("lynxNetwork::read_channel - unknown channelMode");
        transaction_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    Debug_printf("lynxNetwork:receive_channel_json, len:%d %s\n",response_len, response);
    transaction_put(response, response_len);

    return _err;
}

/**
 * LYNX Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to LYNX. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void lynxNetwork::write(uint16_t num_bytes)
{
    Debug_printf("lynxNetwork::write\n");
    memset(response, 0, sizeof(response));

    transaction_get(response, num_bytes);

    *transmitBuffer += string((char *)response, num_bytes);
    err = write_channel(num_bytes) == PROTOCOL_ERROR::NONE ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
}


void lynxNetwork::read()
{
    Debug_printf("lynxNetwork::read\n");
    read_channel();
}



/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return PROTOCOL_ERROR::UNSPECIFIED on error, PROTOCOL_ERROR::NONE on success. Used to emit comlynx_error or comlynx_complete().
 */
protocolError_t lynxNetwork::write_channel(unsigned short num_bytes)
{
    protocolError_t err = PROTOCOL_ERROR::NONE;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->write(num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }

    transaction_complete();
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

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = s.error = NDEV_STATUS::NOT_CONNECTED;
            transaction_error();
        } else {
            err = protocol->status(&s) == PROTOCOL_ERROR::NONE ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
        }
        break;
    case JSON:
        // err = json.status(&status);
        break;
    }

    size_t avail = protocol->available();
    avail = avail > 65535 ? 65535 : avail;
    status.avail = avail;
    status.conn = s.connected;
    status.err = s.error;

    //response_len = sizeof(*status);     // need this? -SJ
    receiveMode = STATUS;               // need this? -SJ

    transaction_put(&status, sizeof(status));
}

/**
 * Get Prefix
 */
void lynxNetwork::get_prefix()
{
    Debug_printf("lynxNetwork::comlynx_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();

    transaction_put(response, response_len);
}

/**
 * Set Prefix
 */
void lynxNetwork::set_prefix(unsigned short len)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));
    transaction_get(&prefixSpec, len);

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
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
    transaction_complete();
}

/**
 * Set login
 */
void lynxNetwork::set_login(uint16_t len)
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));
    transaction_get(loginspec, len);

    login = string((char *)loginspec, len);
    transaction_complete();
}

/**
 * Set password
 */
void lynxNetwork::set_password(uint16_t len)
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));
    transaction_get(passwordspec, len);

    password = string((char *)passwordspec, len);
    transaction_complete();
}

void lynxNetwork::set_channel_mode()
{
    unsigned char m;

    transaction_get(&m, sizeof(m));
    Debug_printf("lynxNetwork::channel_mode - mode: %02X\n", m);

    switch (m)
    {
    case 0:
        channelMode = PROTOCOL;
        transaction_complete();
        break;
    case 1:
        channelMode = JSON;
        transaction_complete();
        break;
    default:
        transaction_error();
        break;
    }
}

void lynxNetwork::json_query(unsigned short len)
{
 /*   uint8_t in[256];
    NetworkStatus ns;

    // get the query
    memset(in, 0, sizeof(in));
    transaction_get(in, len);

    // strip away line endings from input spec.
    for (int i = 0; i < len; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    std::string in_string(reinterpret_cast<char*>(in));
    size_t last_colon_pos = in_string.rfind(':');

    std::string inp_string;
    if (last_colon_pos != std::string::npos) {
        // Skip the device spec. There was a debug message here,
        // but it was removed, because there are cases where
        // removing the devicespec isn't possible, e.g. accessing
        // via CIO (as an XIO). -thom
        inp_string = in_string.substr(last_colon_pos + 1);
    } else {
        inp_string = in_string;
    }

    json.setReadQuery(inp_string, cmdFrame.aux2);
    Debug_printf("lynxNetwork::json_query - (%s)\n", inp_string.c_str());
    read_channel();
    */

    uint8_t in[256];

    // get the query
    memset(in, 0, sizeof(in));
    transaction_get(in, len);

    // strip away line endings from input spec.
    for (int i = 0; i < 256; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    std::string in_string(reinterpret_cast<char*>(in));
    size_t last_colon_pos = in_string.rfind(':');

    std::string inp_string;
    if (last_colon_pos != std::string::npos) {
        // Skip the device spec. There was a debug message here,
        // but it was removed, because there are cases where
        // removing the devicespec isn't possible, e.g. accessing
        // via CIO (as an XIO). -thom
        inp_string = in_string.substr(last_colon_pos + 1);
    } else {
        inp_string = in_string;
    }

    json.setReadQuery(inp_string, cmdFrame.aux2);
    uint16_t json_bytes_remaining = json.available();

    Debug_printf("lynxNetwork::json_query - query: %s\n", inp_string.c_str());
    Debug_printf("lynxNetwork::json_query - json->available: %d\n", json_bytes_remaining);

    std::vector<uint8_t> tmp(json_bytes_remaining);
    json.readValue(tmp.data(), json_bytes_remaining);

    // don't copy past first nul char in tmp
    auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    *receiveBuffer += std::string(tmp.begin(), null_pos);

    Debug_printf("lynxNetwork::json_query - reponse: %s\n", tmp.data());
    transaction_put(tmp.data(), tmp.size());
}

void lynxNetwork::json_parse()
{
    Debug_println("lynxNetwork::json_parse");
    json.parse();
    transaction_complete();
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
        transaction_error();
        return; // Punch out.
    }

    //if (jsonRecvd == false)
    //{
        response_len = json.available();
        response_len = response_len % SERIAL_PACKET_SIZE;
        json.readValue(response, response_len);

        Debug_printf("lynxNetwork:read_channel_json, len:%d %s\n",response_len, response);
        //jsonRecvd = true;
        transaction_put(response, response_len);
    //}
    //else
    //{
    //    if (response_len > 0)
    //        transaction_complete();
    //    else
    //        transaction_error();
    //}
}

void lynxNetwork::read_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

    // Get status
    protocol->status(&ns);
    size_t avail = protocol->available();

    if (!avail)
    {
        transaction_error();
        return;
    }

    // Truncate bytes waiting to response size
    avail = avail % SERIAL_PACKET_SIZE;
    response_len = avail;

    if (protocol->read(response_len) != PROTOCOL_ERROR::NONE) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        transaction_error();
        return;
    }
    else // everything ok
    {
        statusByte.bits.client_error = 0;
        statusByte.bits.client_data_available = response_len > 0;
        memcpy(response, receiveBuffer->data(), response_len);
        receiveBuffer->erase(0, response_len);
        transaction_put(response, response_len);
    }
}

/**
 * Process incoming LYNX command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void lynxNetwork::comlynx_process()
{
    fujiCommandID_t cmd;


    // Get the entire payload from Lynx
    uint16_t len = comlynx_recv_length();
    Debug_printf("lynxNetwork::comlynx_process - len: %ld, ", len);

    comlynx_recv_buffer(recvbuffer, len);
    if (comlynx_recv_ck()) {
        Debug_printf("checksum good\n");
        comlynx_response_ack();        // good checksum
    }
    else {
        Debug_printf(" checksum bad\n");
        comlynx_response_nack();       // good checksum
        return;
    }

    // get command
    transaction_get(&cmd, 1);
    Debug_printf("lynxNetwork::comlynx_process - command: %02X\n", cmd);
    len--;      // we received command already

    switch (cmd)
    {
    case NETCMD_CHDIR:
        set_prefix(len);
        break;
    case NETCMD_GETCWD:
        get_prefix();
        break;
    case NETCMD_OPEN:
        open(len);
        break;
    case NETCMD_CLOSE:
        close();
        break;
    case NETCMD_STATUS:
        status();
        break;
    case NETCMD_READ:
        read();
        break;
    case NETCMD_WRITE:
        write(len);
        break;
    case NETCMD_CHANNEL_MODE:
        set_channel_mode();
        break;
    case NETCMD_PARSE:
    case NETCMD_PARSE_ALT:
        json_parse();
        break;
    case NETCMD_QUERY:
    case NETCMD_QUERY_ALT:
        json_query(len);
        break;
    case NETCMD_USERNAME: // login
        set_login(len);
        break;
    case NETCMD_PASSWORD: // password
        set_password(len);
        break;

    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        process_fs(cmd, len);
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp(cmd);
        break;

    case NETCMD_UNLISTEN:
        process_http(cmd);
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp(cmd);
        break;

    default:
        statusByte.bits.client_error = true;
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
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

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
void lynxNetwork::create_devicespec(string d)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, cmdFrame.aux1 == 6, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void lynxNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void lynxNetwork::parse_and_instantiate_protocol(string d)
{
    create_devicespec(d);
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

/*void lynxNetwork::comlynx_set_timer_rate()
{
}*/

void lynxNetwork::process_fs(fujiCommandID_t cmd, unsigned pkt_len)
{
    comlynx_recv_buffer(response, pkt_len);
    comlynx_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    comlynx_response_ack();

    statusByte.byte = 0x00;

    parse_and_instantiate_protocol(string((char *)response, pkt_len));

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol);
    if (!fs)
    {
        statusByte.bits.client_error = true;
        return;
    }

    protocolError_t cmd_err;
    auto url = urlParser.get();
    switch (cmd)
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
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void lynxNetwork::process_tcp(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol);
    if (!tcp)
    {
        statusByte.bits.client_error = true;
        return;
    }

    protocolError_t cmd_err;
    switch (cmd)
    {
    case NETCMD_CONTROL:
        cmd_err = PROTOCOL_ERROR::NONE;

        // Because we're not handling Adam bus very well, sometimes it
        // retries and we've already accepted which will return an
        // error. Don't do accept if client is already connected.
        {
            NetworkStatus status;
            tcp->status(&status);
            if (!status.connected)
            {
                cmd_err = tcp->accept_connection();
                Debug_printf("ACCEPT %x CHANMODE %d ERR: %d\n", _devnum, channelMode, cmd_err);
            }
        }
        break;
    case NETCMD_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void lynxNetwork::process_http(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol);
    if (!http)
    {
        statusByte.bits.client_error = true;
        return;
    }

    protocolError_t cmd_err;
    switch (cmd)
    {
    case NETCMD_UNLISTEN:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) cmdFrame.aux2);
        break;
    default:
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        return;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        statusByte.bits.client_error = true;
}

void lynxNetwork::process_udp(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol);
    if (!udp)
    {
        statusByte.bits.client_error = true;
        return;
    }

    protocolError_t cmd_err;
    switch (cmd)
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        receiveBuffer->resize(SPECIAL_BUFFER_SIZE);
        cmd_err = udp->get_remote(receiveBuffer->data(), receiveBuffer->size());
        response += *receiveBuffer;
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            uint8_t spData[SPECIAL_BUFFER_SIZE];
            size_t bytes_read = SYSTEM_BUS.read(spData, sizeof(spData));
            cmd_err = udp->set_destination(spData, bytes_read);
            if (cmd_err != PROTOCOL_ERROR::NONE)
                statusByte.bits.client_error = true;
        }
        break;
    default:
        statusByte.bits.client_error = true;
        break;
    }
}

void lynxNetwork::transaction_complete()
{
    Debug_println("transaction_complete - sent ACK");
    comlynx_response_ack();
}

void lynxNetwork::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    comlynx_response_nack();

    // throw away any waiting bytes
    while (SYSTEM_BUS.available() > 0)
        SYSTEM_BUS.read();
}

bool lynxNetwork::transaction_get(void *data, size_t len)
{
    size_t remaining = recvbuffer_len - (recvbuf_pos - recvbuffer);
    size_t to_copy = (len > remaining) ? remaining : len;

    memcpy(data, recvbuf_pos, to_copy);
    recvbuf_pos += to_copy;

    return len;
}

void lynxNetwork::transaction_put(const void *data, size_t len, bool err)
{
    uint8_t b;

    // set response buffer
    memcpy(response, data, len);
    response_len = len;

    // send all data back to Lynx
    uint8_t ck = comlynx_checksum(response, response_len);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(ck);

    // get ACK or NACK from Lynx, we're ignoring currently
    //uint8_t t = comlynx_recv_timeout(&b, 8000);
    uint8_t r = comlynx_recv();
    #ifdef DEBUG
        //if (!t)
            if (r == FUJICMD_ACK)
                Debug_println("transaction_put - Lynx ACKed");
            else
                Debug_println("transaction put - Lynx NAKed");
        //else
        //    Debug_println("transaction_put - timed out waiting for ACK/NAK from Lynx");
    #endif

    return;
}

#endif /* BUILD_LYNX */
