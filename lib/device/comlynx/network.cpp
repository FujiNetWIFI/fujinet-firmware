#ifdef BUILD_LYNX

/**
 * N: Firmware
 */

#include "network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"

#include "utils.h"

#include "status_error_codes.h"
#include "TCP.h"
#include "UDP.h"
#include "Test.h"
#include "Telnet.h"
#include "TNFS.h"
#include "FTP.h"
#include "HTTP.h"
#include "SSH.h"
#include "SMB.h"

// using namespace std;

/**
 * Constructor
 */
lynxNetwork::lynxNetwork()
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

    if (receiveBuffer != nullptr)
        delete receiveBuffer;
    if (transmitBuffer != nullptr)
        delete transmitBuffer;
    if (specialBuffer != nullptr)
        delete specialBuffer;
}

/** LYNX COMMANDS ***************************************************************/

/**
 * LYNX Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void lynxNetwork::open(unsigned short s)
{
    uint8_t _aux1 = comlynx_recv();
    uint8_t _aux2 = comlynx_recv();
    string d;

    s--;
    s--;

    memset(response, 0, sizeof(response));
    comlynx_recv_buffer(response, s);
    comlynx_recv(); // checksum

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

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

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("open()\n");

    // Parse and instantiate protocol
    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser, &cmdFrame) == true)
    {
        statusByte.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        delete protocol;
        protocol = nullptr;
        return;
    }

    // Associate channel mode
    json.setProtocol(protocol);
}

/**
 * LYNX Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void lynxNetwork::close()
{
    Debug_printf("lynxNetwork::close()\n");

    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    statusByte.byte = 0x00;

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
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool lynxNetwork::read_channel(unsigned short num_bytes)
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
 * LYNX Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to LYNX. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void lynxNetwork::write(uint16_t num_bytes)
{
    Debug_printf("!!! WRITE\n");
    memset(response, 0, sizeof(response));

    comlynx_recv_buffer(response, num_bytes);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    *transmitBuffer += string((char *)response, num_bytes);
    err = comlynx_write_channel(num_bytes);
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit comlynx_error or comlynx_complete().
 */
bool lynxNetwork::comlynx_write_channel(unsigned short num_bytes)
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
 * LYNX Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to LYNX.
 */
void lynxNetwork::status()
{
    NetworkStatus s;
    comlynx_recv(); // CK
    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->status(&s);
        break;
    case JSON:
        // err = json.status(&status);
        break;
    }

    response[0] = s.rxBytesWaiting & 0xFF;
    response[1] = s.rxBytesWaiting >> 8;
    response[2] = s.connected;
    response[3] = s.error;
    response_len = 4;
    receiveMode = STATUS;
}

/**
 * Get Prefix
 */
void lynxNetwork::get_prefix()
{
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    Debug_printf("lynxNetwork::comlynx_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();
}

/**
 * Set Prefix
 */
void lynxNetwork::set_prefix(unsigned short s)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    comlynx_recv_buffer(prefixSpec, s);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("lynxNetwork::comlynx_set_prefix(%s)\n", prefixSpec_str.c_str());

    if (prefixSpec_str == "..") // Devance path N:..
    {
        vector<int> pathLocations;
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
}

/**
 * Set login
 */
void lynxNetwork::set_login(uint16_t s)
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));

    comlynx_recv_buffer(loginspec, s);
    comlynx_recv(); // ck

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    login = string((char *)loginspec, s);
}

/**
 * Set password
 */
void lynxNetwork::set_password(uint16_t s)
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));

    comlynx_recv_buffer(passwordspec, s);
    comlynx_recv(); // ck

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    password = string((char *)passwordspec, s);
}

void lynxNetwork::del(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    comlynx_recv_buffer(response, s);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
        return;

    cmdFrame.comnd = '!';

    if (protocol->perform_idempotent_80(urlParser, &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void lynxNetwork::rename(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    comlynx_recv_buffer(response, s);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = ' ';

    if (protocol->perform_idempotent_80(urlParser, &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void lynxNetwork::mkdir(uint16_t s)
{
    string d;

    memset(response, 0, sizeof(response));
    comlynx_recv_buffer(response, s);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = '*';

    if (protocol->perform_idempotent_80(urlParser, &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void lynxNetwork::channel_mode()
{
    unsigned char m = comlynx_recv();
    comlynx_recv(); // CK

    switch (m)
    {
    case 0:
        channelMode = PROTOCOL;
        ComLynx.start_time = esp_timer_get_time();
        comlynx_response_ack();
        break;
    case 1:
        channelMode = JSON;
        ComLynx.start_time = esp_timer_get_time();
        comlynx_response_ack();
        break;
    default:
        ComLynx.start_time = esp_timer_get_time();
        comlynx_response_nack();
        break;
    }

    Debug_printf("lynxNetwork::channel_mode(%u)\n", m);
    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();
}

void lynxNetwork::json_query(unsigned short s)
{
    uint8_t *c = (uint8_t *)malloc(s);

    comlynx_recv_buffer(c, s);
    comlynx_recv(); // CK

    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();

    json.setReadQuery(std::string((char *)c, s),cmdframe.aux2);

    Debug_printf("lynxNetwork::json_query(%s)\n", c);

    free(c);
}

void lynxNetwork::json_parse()
{
    comlynx_recv(); // CK
    ComLynx.start_time = esp_timer_get_time();
    comlynx_response_ack();
    json.parse();
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: LYNX call.
 */
void lynxNetwork::comlynx_special_inquiry()
{
}

void lynxNetwork::do_inquiry(unsigned char inq_cmd)
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
        case 0x20:
        case 0x21:
        case 0x23:
        case 0x24:
        case 0x2A:
        case 0x2B:
        case 0x2C:
        case 0xFD:
        case 0xFE:
            inq_dstats = 0x80;
            break;
        case 0x30:
            inq_dstats = 0x40;
            break;
        case 'Z': // Set interrupt rate
            inq_dstats = 0x00;
            break;
        case 'T': // Set Translation
            inq_dstats = 0x00;
            break;
        case 0x80: // JSON Parse
            inq_dstats = 0x00;
            break;
        case 0x81: // JSON Query
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
 * and based on the return, signal comlynx_complete() or error().
 */
void lynxNetwork::comlynx_special_00(unsigned short s)
{
    cmdFrame.aux1 = comlynx_recv();
    cmdFrame.aux2 = comlynx_recv();

    ComLynx.start_time = esp_timer_get_time();

    if (protocol->special_00(&cmdFrame) == false)
        comlynx_response_ack();
    else
        comlynx_response_nack();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void lynxNetwork::comlynx_special_40(unsigned short s)
{
    cmdFrame.aux1 = comlynx_recv();
    cmdFrame.aux2 = comlynx_recv();

    if (protocol->special_40(response, 1024, &cmdFrame) == false)
        comlynx_response_ack();
    else
        comlynx_response_nack();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void lynxNetwork::comlynx_special_80(unsigned short s)
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    cmdFrame.aux1 = comlynx_recv();
    cmdFrame.aux2 = comlynx_recv();
    comlynx_recv_buffer(spData, s);

    Debug_printf("lynxNetwork::comlynx_special_80() - %s\n", spData);

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
        comlynx_response_ack();
    else
        comlynx_response_nack();
}

void lynxNetwork::comlynx_response_status()
{
    NetworkStatus s;

    if (protocol != nullptr)
        protocol->status(&s);

    statusByte.bits.client_connected = s.connected == true;
    statusByte.bits.client_data_available = s.rxBytesWaiting > 0;
    statusByte.bits.client_error = s.error > 1;

    status_response[1] = 2; // max packet size 1026 bytes, maybe larger?
    status_response[2] = 4;

    status_response[4] = statusByte.byte;

    int64_t t = esp_timer_get_time() - ComLynx.start_time;

    if (t < 300)
        virtualDevice::comlynx_response_status();
}

void lynxNetwork::comlynx_control_ack()
{
}

void lynxNetwork::comlynx_control_send()
{
    uint16_t s = comlynx_recv_length(); // receive length
    uint8_t c = comlynx_recv();         // receive command

    s--; // Because we've popped the command off the stack

    switch (c)
    {
    case ' ':
        rename(s);
        break;
    case '!':
        del(s);
        break;
    case '*':
        mkdir(s);
        break;
    case ',':
        set_prefix(s);
        break;
    case '0':
        get_prefix();
        break;
    case 'O':
        open(s);
        break;
    case 'C':
        close();
        break;
    case 'S':
        status();
        break;
    case 'W':
        write(s);
        break;
    case 0xFC:
        channel_mode();
        break;
    case 0xFD: // login
        set_login(s);
        break;
    case 0xFE: // password
        set_password(s);
        break;
    default:
        switch (channelMode)
        {
        case PROTOCOL:
            if (inq_dstats == 0x00)
                comlynx_special_00(s);
            else if (inq_dstats == 0x40)
                comlynx_special_40(s);
            else if (inq_dstats == 0x80)
                comlynx_special_80(s);
            else
                Debug_printf("comlynx_control_send() - Unknown Command: %02x\n", c);
            break;
        case JSON:
            switch (c)
            {
            case 'P':
                json_parse();
                break;
            case 'Q':
                json_query(s);
                break;
            }
            break;
        default:
            Debug_printf("Unknown channel mode\n");
            break;
        }
        do_inquiry(c);
    }
}

void lynxNetwork::comlynx_control_clr()
{
    comlynx_response_send();

    if (channelMode == JSON)
        jsonRecvd = false;
}

void lynxNetwork::comlynx_control_receive_channel()
{
    switch (channelMode)
    {
    case JSON:
        comlynx_control_receive_channel_json();
        break;
    case PROTOCOL:
        comlynx_control_receive_channel_protocol();
        break;
    }
}

void lynxNetwork::comlynx_control_receive_channel_json()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    if (jsonRecvd == false)
    {
        response_len = json.readValueLen();
        json.readValue(response,response_len);
        jsonRecvd=true;
        comlynx_response_ack();
    }
    else
    {
        ComLynx.start_time = esp_timer_get_time();
        if (response_len > 0)
            comlynx_response_ack();
        else
            comlynx_response_nack();
    }
}

void lynxNetwork::comlynx_control_receive_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    // Get status
    protocol->status(&ns);
    Debug_printf("!!! rxBytesWaiting: %d\n",ns.rxBytesWaiting);
    if (ns.rxBytesWaiting > 0)
        comlynx_response_ack();
    else
    {
        comlynx_response_nack();
        return;
    }

    // Truncate bytes waiting to response size
    ns.rxBytesWaiting = (ns.rxBytesWaiting > 1024) ? 1024 : ns.rxBytesWaiting;
    response_len = ns.rxBytesWaiting;

    if (protocol->read(response_len)) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
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

void lynxNetwork::comlynx_control_receive()
{
    ComLynx.start_time = esp_timer_get_time();

    // Data is waiting, go ahead and send it off.
    if (response_len > 0)
    {
        comlynx_response_ack();
        return;
    }

    switch (receiveMode)
    {
    case CHANNEL:
        comlynx_control_receive_channel();
        break;
    case STATUS:
        break;
    }
}

void lynxNetwork::comlynx_response_send()
{
    uint8_t c = comlynx_checksum(response, response_len);

    comlynx_send(0xB0 | _devnum);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(c);

    Debug_printf("comlynx_response_send: %s\n",response);
    memset(response, 0, response_len);
    response_len = 0;
}

/**
 * Process incoming LYNX command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void lynxNetwork::comlynx_process(uint8_t b)
{
    unsigned char c = b >> 4; // Seperate out command from node ID

    switch (c)
    {
    case MN_STATUS:
        comlynx_control_status();
        break;
    case MN_ACK:
        comlynx_control_ack();
        break;
    case MN_CLR:
        comlynx_control_clr();
        break;
    case MN_RECEIVE:
        comlynx_control_receive();
        break;
    case MN_SEND:
        comlynx_control_send();
        break;
    case MN_READY:
        comlynx_control_ready();
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
    if (urlParser == nullptr)
    {
        Debug_printf("lynxNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
        return false; // error.
    }

    // Convert to uppercase
    std::transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

    if (urlParser->scheme == "TCP")
    {
        protocol = new NetworkProtocolTCP(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "UDP")
    {
        protocol = new NetworkProtocolUDP(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "TEST")
    {
        protocol = new NetworkProtocolTest(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "TELNET")
    {
        protocol = new NetworkProtocolTELNET(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "TNFS")
    {
        protocol = new NetworkProtocolTNFS(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "FTP")
    {
        protocol = new NetworkProtocolFTP(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "HTTP" || urlParser->scheme == "HTTPS")
    {
        protocol = new NetworkProtocolHTTP(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "SSH")
    {
        protocol = new NetworkProtocolSSH(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else if (urlParser->scheme == "SMB")
    {
        protocol = new NetworkProtocolSMB(receiveBuffer, transmitBuffer, specialBuffer);
    }
    else
    {
        Debug_printf("Invalid protocol: %s\n", urlParser->scheme.c_str());
        return false; // invalid protocol.
    }

    if (protocol == nullptr)
    {
        Debug_printf("lynxNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    if (!login.empty())
    {
        protocol->login = &login;
        protocol->password = &password;
    }

    Debug_printf("lynxNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}

void lynxNetwork::parse_and_instantiate_protocol(string d)
{
    deviceSpec = d;

    // Invalid URL returns error 165 in status.
    if (parseURL() == false)
    {
        Debug_printf("Invalid devicespec: %s\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_INVALID_DEVICESPEC;
        return;
    }

    Debug_printf("Parse and instantiate protocol: %s\n", deviceSpec.c_str());

    // Instantiate protocol object.
    if (instantiate_protocol() == false)
    {
        Debug_printf("Could not open protocol.\n");
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_GENERAL;
        return;
    }
}

/**
 * Is this a valid URL? (Used to generate ERROR 165)
 */
bool lynxNetwork::isValidURL(EdUrlParser *url)
{
    if (url->scheme == "")
        return false;
    else if ((url->path == "") && (url->port == ""))
        return false;
    else
        return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 *
 * The resulting URL is then sent into EdURLParser to get our URLParser object which is used in the rest
 * of lynxNetwork.
 *
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool lynxNetwork::parseURL()
{
    string url;
    string unit = deviceSpec.substr(0, deviceSpec.find_first_of(":") + 1);

    if (urlParser != nullptr)
        delete urlParser;

    // Prepend prefix, if set.
    if (prefix.length() > 0)
        deviceSpec = unit + prefix + deviceSpec.substr(deviceSpec.find(":") + 1);
    else
        deviceSpec = unit + deviceSpec.substr(string(deviceSpec).find(":") + 1);

    Debug_printf("lynxNetwork::parseURL(%s)\n", deviceSpec.c_str());

    // Strip non-ascii characters.
    util_strip_nonascii(deviceSpec);

    // Process comma from devicespec (DOS 2 COPY command)
    // processCommaFromDevicespec();

    if (cmdFrame.aux1 != 6) // Anything but a directory read...
    {
        std::replace(deviceSpec.begin(), deviceSpec.end(), '*', '\0'); // FIXME: Come back here and deal with WC's
    }

    // // Some FMSes add a dot at the end, remove it.
    // if (deviceSpec.substr(deviceSpec.length() - 1) == ".")
    //     deviceSpec.erase(deviceSpec.length() - 1, string::npos);

    // Remove any spurious spaces
    deviceSpec = util_remove_spaces(deviceSpec);

    // chop off front of device name for URL, and parse it.
    url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = EdUrlParser::parseUrl(url);

    Debug_printf("lynxNetwork::parseURL transformed to (%s, %s)\n", deviceSpec.c_str(), url.c_str());

    return isValidURL(urlParser);
}

void lynxNetwork::comlynx_set_translation()
{
    // trans_aux2 = cmdFrame.aux2;
    // comlynx_complete();
}

void lynxNetwork::comlynx_set_timer_rate()
{
    // timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // // Stop extant timer
    // timer_stop();

    // // Restart timer if we're running a protocol.
    // if (protocol != nullptr)
    //     timer_start();

    // comlynx_complete();
}

#endif /* BUILD_LYNX */