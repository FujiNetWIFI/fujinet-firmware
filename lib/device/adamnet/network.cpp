#ifdef BUILD_ADAM

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

using namespace std;

/**
 * Constructor
 */
adamNetwork::adamNetwork()
{
    status_response[1] = 0x00;
    status_response[2] = 0x04; // 1024 bytes
    status_response[3] = 0x00; // Character device

    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    protocol = nullptr;

    json.setLineEnding("\x00");
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
        delete protocol;

    protocol = nullptr;
}

/** ADAM COMMANDS ***************************************************************/

/**
 * @brief get error number from protocol adapter
 */
void adamNetwork::get_error()
{
    NetworkStatus ns;

    Debug_printf("Get Error\n");
    adamnet_recv(); // CK
    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();
    response_len = 1;

    if (protocol == nullptr)
    {
        response[0] = NETWORK_ERROR_NOT_CONNECTED;
    }
    else
    {
        protocol->status(&ns);
        response[0] = ns.error;
    }
}
/**
 * ADAM Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void adamNetwork::open(unsigned short s)
{
    uint8_t _aux1 = adamnet_recv();
    uint8_t _aux2 = adamnet_recv();
    string d;

    s--;
    s--;

    memset(response, 0, sizeof(response));
    adamnet_recv_buffer(response, s);
    adamnet_recv(); // checksum

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

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
    }

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("open()\n");

    // Parse and instantiate protocol
    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), (netProtoOpenMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2) == true)
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
        return;
    }

    // Associate channel mode
    json.setProtocol(protocol);

    // Clear response
    memset(response, 0, sizeof(response));
    response_len = 0;
}

/**
 * ADAM Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void adamNetwork::close()
{
    Debug_printf("adamNetwork::close()\n");

    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    statusByte.byte = 0x00;

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

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    memset(response, 0, sizeof(response));
    response_len = 0;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool adamNetwork::read_channel(unsigned short num_bytes)
{
    bool _err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        _err = protocol->read(num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        _err = true;
        break;
    }
    return _err;
}

/**
 * ADAM Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to ADAM. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void adamNetwork::write(uint16_t num_bytes)
{
    Debug_printf("!!! WRITE\n");
    memset(response, 0, sizeof(response));

    adamnet_recv_buffer(response, num_bytes);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    *transmitBuffer += string((char *)response, num_bytes);
    err = adamnet_write_channel(num_bytes);
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit adamnet_error or adamnet_complete().
 */
bool adamNetwork::adamnet_write_channel(unsigned short num_bytes)
{
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->write(num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        err = true;
        break;
    }
    return err;
}

/**
 * ADAM Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to ADAM.
 */
void adamNetwork::status()
{
    NetworkStatus ns;
    NDeviceStatus *status = (NDeviceStatus *) response;
    adamnet_recv(); // CK
    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    if (protocol == nullptr)
    {
        status->avail = 0;
        status->conn = 0;
        status->err = 165; // invalid spec.
        response_len = sizeof(*status);
        return;
    }

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->status(&ns);
        break;
    case JSON:
        // err = json.status(&status);
        break;
    }

    size_t avail = protocol->available();
    avail = avail > 65535 ? 65535 : avail;
    status->avail = avail;
    status->conn = ns.connected;
    status->err = ns.error;
    response_len = sizeof(*status);
    receiveMode = STATUS;
}

/**
 * Get Prefix
 */
void adamNetwork::get_prefix()
{
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    Debug_printf("adamNetwork::adamnet_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();
}

/**
 * Set Prefix
 */
void adamNetwork::set_prefix(unsigned short s)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    adamnet_recv_buffer(prefixSpec, s);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    prefixSpec_str = string((const char *)prefixSpec);
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

    response_len = 0;
    memset(response, 0, sizeof(response));
}

/**
 * Set login
 */
void adamNetwork::set_login(uint16_t s)
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));

    adamnet_recv_buffer(loginspec, s);
    adamnet_recv(); // ck

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    login = string((char *)loginspec, s);
}

/**
 * Set password
 */
void adamNetwork::set_password(uint16_t s)
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));

    adamnet_recv_buffer(passwordspec, s);
    adamnet_recv(); // ck

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    password = string((char *)passwordspec, s);
}

void adamNetwork::del(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    adamnet_recv_buffer(response, s);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
        return;

    cmdFrame.comnd = '!';

    if (protocol->perform_idempotent_80(urlParser.get(), (fujiCommandID_t) cmdFrame.comnd) == true)
    {
        statusByte.bits.client_error = true;
        return;
    }

    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamNetwork::rename(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    adamnet_recv_buffer(response, s);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = ' ';

    if (protocol->perform_idempotent_80(urlParser.get(), (fujiCommandID_t) cmdFrame.comnd) == true)
    {
        statusByte.bits.client_error = true;
        return;
    }

    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamNetwork::mkdir(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    adamnet_recv_buffer(response, s);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = '*';

    if (protocol->perform_idempotent_80(urlParser.get(), (fujiCommandID_t) cmdFrame.comnd) == true)
    {
        statusByte.bits.client_error = true;
        return;
    }

    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamNetwork::channel_mode()
{
    unsigned char m = adamnet_recv();
    adamnet_recv(); // CK

    switch (m)
    {
    case 0:
        channelMode = PROTOCOL;
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_ack();
        break;
    case 1:
        channelMode = JSON;
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_ack();
        break;
    default:
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_nack();
        break;
    }

    Debug_printf("adamNetwork::channel_mode(%u)\n", m);
    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamNetwork::json_query(unsigned short s)
{
    memset(response, 0, sizeof(response));
    response_len = 0;

    adamnet_recv_buffer(response, s);
    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    json.setReadQuery(std::string((char *)response, s), cmdFrame.aux2);

    Debug_printv("adamNetwork::json_query(%s)\n", response);
}

void adamNetwork::json_parse()
{
    adamnet_recv(); // CK
    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();
    json.parse();
    memset(response, 0, sizeof(response));
    response_len = 0;
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: ADAM call.
 */
void adamNetwork::adamnet_special_inquiry()
{
}

void adamNetwork::do_inquiry(fujiCommandID_t inq_cmd)
{
    // Reset inq_dstats
    inq_dstats = 0xff;

    cmdFrame.comnd = inq_cmd;

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
        inq_dstats = protocol->special_inquiry(inq_cmd);

    // If we didn't get one from protocol, or unsupported, see if supported globally.
    if (inq_dstats == 0xFF)
    {
        switch (inq_cmd)
        {
        case FUJICMD_RENAME:
        case FUJICMD_DELETE:
        case FUJICMD_LOCK:
        case FUJICMD_UNLOCK:
        case FUJICMD_MKDIR:
        case FUJICMD_RMDIR:
        case FUJICMD_CHDIR:
        case FUJICMD_SCAN_NETWORKS:
        case FUJICMD_GET_SSID:
            inq_dstats = 0x80;
            break;
        case FUJICMD_GETCWD:
            inq_dstats = 0x40;
            break;
        case FUJICMD_TIMER: // Set interrupt rate
            inq_dstats = 0x00;
            break;
        case FUJICMD_TRANSLATION:
            inq_dstats = 0x00;
            break;
        case FUJICMD_JSON_PARSE:
            inq_dstats = 0x00;
            break;
        case FUJICMD_JSON_QUERY:
            inq_dstats = 0x80;
            break;
        default:
            inq_dstats = 0xFF; // not supported
            break;
        }
    }

    Debug_printf("inq_dstats = %u\n", inq_dstats);
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal adamnet_complete() or error().
 */
void adamNetwork::adamnet_special_00(unsigned short s)
{
    cmdFrame.aux1 = adamnet_recv();
    cmdFrame.aux2 = adamnet_recv();

    adamnet_recv(); // CK

    SYSTEM_BUS.start_time = esp_timer_get_time();
    adamnet_response_ack();

    protocol->special_00((fujiCommandID_t) cmdFrame.comnd, cmdFrame.aux2);
    inq_dstats = 0xff;

    response_len = 0;
    memset(response, 0, sizeof(response));
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void adamNetwork::adamnet_special_40(unsigned short s)
{
    cmdFrame.aux1 = adamnet_recv();
    cmdFrame.aux2 = adamnet_recv();

    adamnet_recv(); // CK

    if (protocol->special_40(response, 1024, (fujiCommandID_t) cmdFrame.comnd) == false)
        adamnet_response_ack();
    else
        adamnet_response_nack();

    inq_dstats = 0xff;

    response_len = 0;
    memset(response, 0, sizeof(response));
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void adamNetwork::adamnet_special_80(unsigned short s)
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    cmdFrame.aux1 = adamnet_recv();
    cmdFrame.aux2 = adamnet_recv();
    adamnet_recv_buffer(spData, s);

    Debug_printf("adamNetwork::adamnet_special_80() - %s\n", spData);

    adamnet_recv(); // CK

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, (fujiCommandID_t) cmdFrame.comnd) == false)
        adamnet_response_ack();
    else
        adamnet_response_nack();
    inq_dstats = 0xff;

    memset(response, 0, sizeof(response));
    response_len = 0;
}

void adamNetwork::adamnet_response_status()
{
    NetworkStatus s;

    if (protocol != nullptr)
    {
        protocol->status(&s);
        statusByte.bits.client_connected = s.connected == true;
        statusByte.bits.client_data_available = protocol->available() > 0;
        statusByte.bits.client_error = s.error > 1;
    }

    status_response[1] = 2; // max packet size 1026 bytes, maybe larger?
    status_response[2] = 4;

    status_response[4] = statusByte.byte;

    int64_t t = esp_timer_get_time() - SYSTEM_BUS.start_time;

    if (t < 300)
        virtualDevice::adamnet_response_status();
}

void adamNetwork::adamnet_control_ack()
{
}

void adamNetwork::adamnet_control_send()
{
    uint16_t s = adamnet_recv_length(); // receive length
    fujiCommandID_t c = (fujiCommandID_t) adamnet_recv();         // receive command

    s--; // Because we've popped the command off the stack

    switch (c)
    {
    case FUJICMD_RENAME:
        rename(s);
        break;
    case FUJICMD_DELETE:
        del(s);
        break;
    case FUJICMD_MKDIR:
        mkdir(s);
        break;
    case FUJICMD_CHDIR:
        set_prefix(s);
        break;
    case FUJICMD_GETCWD:
        get_prefix();
        break;
    case FUJICMD_GET_ERROR:
        get_error();
        break;
    case FUJICMD_OPEN:
        open(s);
        break;
    case FUJICMD_CLOSE:
        close();
        break;
    case FUJICMD_STATUS:
        status();
        break;
    case FUJICMD_WRITE:
        write(s);
        break;
    case FUJICMD_JSON:
        channel_mode();
        break;
    case FUJICMD_USERNAME: // login
        set_login(s);
        break;
    case FUJICMD_PASSWORD: // password
        set_password(s);
        break;
    default:
        Debug_printf("fall through to default\n");
        switch (channelMode)
        {
        case PROTOCOL:
            do_inquiry(c); // set inq_dstats

            if (inq_dstats == 0x00)
                adamnet_special_00(s);
            else if (inq_dstats == 0x40)
                adamnet_special_40(s);
            else if (inq_dstats == 0x80)
                adamnet_special_80(s);
            else
                Debug_printf("adamnet_control_send() - Unknown Command: %02x\n", c);
            break;
        case JSON:
            switch (c)
            {
            case FUJICMD_PARSE:
                json_parse();
                break;
            case FUJICMD_QUERY:
                json_query(s);
                break;
            default:
                break;
            }
            break;
        default:
            Debug_printf("Unknown channel mode\n");
            break;
        }
    }
}

void adamNetwork::adamnet_control_clr()
{
    adamnet_response_send();

    if (channelMode == JSON)
        jsonRecvd = false;
}

void adamNetwork::adamnet_control_receive_channel_json()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    if (jsonRecvd == false)
    {
        response_len = json.readValueLen();
        json.readValue(response, response_len);
        jsonRecvd = true;
        adamnet_response_ack();
    }
    else
    {
        SYSTEM_BUS.start_time = esp_timer_get_time();
        if (response_len > 0)
            adamnet_response_ack();
        else
            adamnet_response_nack();
    }
}

inline void adamNetwork::adamnet_control_receive_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
    {
        adamnet_response_nack(true);
        return; // Punch out.
    }

    // Get status
    protocol->status(&ns);
    size_t avail = protocol->available();

    if (!avail)
    {
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_nack(true);
        return;
    }
    else
    {
        SYSTEM_BUS.start_time = esp_timer_get_time();
        adamnet_response_ack(true);
    }

    // Truncate bytes waiting to response size
    avail = avail > 1024 ? 1024 : avail;
    response_len = avail;

    if (protocol->read(response_len)) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        adamnet_response_nack();
        return;
    }
    else // everything ok
    {
        statusByte.bits.client_error = 0;
        statusByte.bits.client_data_available = response_len > 0;
        memcpy(response, receiveBuffer->data(), response_len);
        receiveBuffer->erase(0, response_len);
    }
}

inline void adamNetwork::adamnet_control_receive()
{
    SYSTEM_BUS.start_time = esp_timer_get_time();

    // Data is waiting, go ahead and send it off.
    if (response_len > 0)
    {
        adamnet_response_ack();
        return;
    }

    switch (channelMode)
    {
    case JSON:
        adamnet_control_receive_channel_json();
        break;
    case PROTOCOL:
        adamnet_control_receive_channel_protocol();
        break;
    }
}

void adamNetwork::adamnet_response_send()
{
    uint8_t c = adamnet_checksum(response, response_len);

    if (response_len)
    {
        adamnet_send(0xB0 | _devnum);
        adamnet_send_length(response_len);
        adamnet_send_buffer(response, response_len);
        adamnet_send(c);
    }
    else
        adamnet_send(0xC0 | _devnum); // NAK!

    memset(response, 0, response_len);
    response_len = 0;
}

/**
 * Process incoming ADAM command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void adamNetwork::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4; // Seperate out command from node ID

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_ACK:
        adamnet_control_ack();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        adamnet_control_receive();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool adamNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

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
void adamNetwork::create_devicespec(string d)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, cmdFrame.aux1 == 6, false);
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

void adamNetwork::parse_and_instantiate_protocol(string d)
{
    create_devicespec(d);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: %s\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_INVALID_DEVICESPEC;
        return;
    }

    Debug_printf("::parse_and_instantiate_protocol transformed to (%s, %s)\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol.\n");
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_GENERAL;
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

#endif /* BUILD_ADAM */
