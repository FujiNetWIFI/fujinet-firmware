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

#define VERBOSE_PROTOCOL



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
        delete protocol;
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
void lynxNetwork::open(unsigned short len)
{
    uint8_t _aux1;
    uint8_t _aux2;
    string d;


    transaction_get(&_aux1, sizeof(_aux1));
    transaction_get(&_aux2, sizeof(_aux2));

    memset(response, 0, sizeof(response));
    transaction_get(&response, len);

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

    Debug_printf("lynxNetwork::open - aux1:%02X aux2:%02X %s\n", open_aux1, open_aux2, response);

    // Parse and instantiate protocol
    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        transaction_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), (fileAccessMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2) != FUJI_ERROR::NONE)
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

    // setup JSON
    json = new FNJSON();
    json->setLineEnding("\x00");        // null terminate json values always
    json->setProtocol(protocol);
    channelMode = PROTOCOL;

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

    // delete the json object
    if (json != nullptr)
    {
        delete json;
        json = nullptr;
    }

    transaction_complete();
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
        transaction_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    switch (channelMode)
    {
    case PROTOCOL:
        read_channel_protocol();
        break;
    case JSON:
        response_len = json->available();
        response_len = response_len % SERIAL_PACKET_SIZE;
        json->readValue(response, response_len);

        _err = FUJI_ERROR::NONE;
        break;
    default:
        Debug_println("lynxNetwork::read_channel - unknown channelMode");
        transaction_error();
        return FUJI_ERROR::UNSPECIFIED;
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
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

    Debug_printf("lynxNetwork::write\n");
    memset(response, 0, sizeof(response));

    transaction_get(response, num_bytes);

    *transmitBuffer += string((char *)response, num_bytes);
    err = write_channel(num_bytes) == FUJI_ERROR::NONE ? NDEV_STATUS::SUCCESS : NDEV_STATUS::GENERAL;
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
        transaction_error();
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

    Debug_println("lynxNetwork::status");

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = s.error = NDEV_STATUS::NOT_CONNECTED;
            transaction_error();
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

    Debug_printf("lynxNetwork::comlynx_status - avail:%d conn:%d err:%d\n", status.avail, status.conn, status.err);
    transaction_put(&status, sizeof(status));
}

/**
 * Get Prefix
 */
void lynxNetwork::get_prefix()
{
    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

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

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

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

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

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

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

    memset(passwordspec, 0, sizeof(passwordspec));
    transaction_get(passwordspec, len);

    password = string((char *)passwordspec, len);
    transaction_complete();
}

void lynxNetwork::set_channel_mode()
{
    unsigned char m;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

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
    std::string in(len, '\0');

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

    // get the query string
    transaction_get(in.data(), len);
    Debug_printf("lynxNetwork::json_query - query:%s\n", in.c_str());

    // read the json value from query, there may be more bytes than we can transfer
    // in one response
    json->setReadQuery(in, cmdFrame.aux2);
    uint16_t jsonlen = json->available();
    jsonlen = std::min<uint16_t>(json->available(), SERIAL_PACKET_SIZE);
    Debug_printf("lynxNetwork::json_query - json->available:%d, len:%d\n", json->available(), jsonlen);

    if (jsonlen == 0) {
      transaction_error();
      return;
    }

    std::vector<uint8_t> tmp(jsonlen);
    json->readValue(tmp.data(), jsonlen);

    // don't copy past first nul char in tmp (don't think this is needed -SJ)
    //auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    //*receiveBuffer += std::string(tmp.begin(), null_pos);

    Debug_printf("lynxNetwork::json_query - value:%.*s\n", static_cast<int>(jsonlen), reinterpret_cast<const char*>(tmp.data()));
    transaction_put(tmp.data(), tmp.size());
}

void lynxNetwork::json_parse()
{
  if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return; // Punch out.
    }

    Debug_println("lynxNetwork::json_parse");
    json->parse();
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
        return;
    }

    // check how many bytes available and truncated to packet size
    response_len = json->available();
    response_len = std::min<uint16_t>(response_len, SERIAL_PACKET_SIZE);

    if (response_len == 0) {
      transaction_error();
      return;
    }

    json->readValue(response, response_len);
    Debug_printf("lynxNetwork:read_channel_json, len:%d %s\n",response_len, response);
    transaction_put(response, response_len);
}

void lynxNetwork::read_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr)) {
        transaction_error();
        return;
    }

    // Get status
    protocol->status(&ns);
    size_t avail = protocol->available();

    Debug_printf("lynxNetwork:read_channel_protocol - protcol->available:%d\n", avail);

    if (!avail)
    {
        transaction_error();
        return;
    }

    // Truncate bytes waiting to response size
    avail = std::min<uint16_t>(avail, SERIAL_PACKET_SIZE);
    response_len = avail;

    if (protocol->read(response_len) != FUJI_ERROR::NONE) // protocol adapter returned error
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
        Debug_printf("lynxnetwork::comlynx_process - unknown command: %02X", cmd);
        transaction_error();
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
    transaction_get(response, pkt_len);
    statusByte.byte = 0x00;

    parse_and_instantiate_protocol(string((char *)response, pkt_len));

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol);
    if (!fs)
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    fujiError_t cmd_err;
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
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE) {
        transaction_error();
        statusByte.bits.client_error = true;
    }
    else
        transaction_complete();
}

void lynxNetwork::process_tcp(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol);
    if (!tcp)
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    fujiError_t cmd_err;
    switch (cmd)
    {
    case NETCMD_CONTROL:
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
                Debug_printf("ACCEPT %x CHANMODE %d ERR: %d\n", _devnum, channelMode, cmd_err);
            }
        }
        break;
    case NETCMD_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE) {
        statusByte.bits.client_error = true;
        transaction_error();
    }
    else
        transaction_complete();
}

void lynxNetwork::process_http(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol);
    if (!http)
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    fujiError_t cmd_err;
    switch (cmd)
    {
    case NETCMD_UNLISTEN:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) cmdFrame.aux2);
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        return;
    }

    if (cmd_err != FUJI_ERROR::NONE) {
        statusByte.bits.client_error = true;
        transaction_error();
    }
    else
        transaction_complete();
}

void lynxNetwork::process_udp(fujiCommandID_t cmd)
{
    statusByte.byte = 0x00;

    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol);
    if (!udp)
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    fujiError_t cmd_err;
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
            if (cmd_err != FUJI_ERROR::NONE)
                statusByte.bits.client_error = true;
        }
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        statusByte.bits.client_error = true;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        transaction_error();
    else
        transaction_complete();
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
