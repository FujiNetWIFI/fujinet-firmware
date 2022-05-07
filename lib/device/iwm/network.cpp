#ifdef BUILD_APPLE

/**
 * N: Firmware
 */

#include "network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"
#include "../../hardware/led.h"

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
iwmNetwork::iwmNetwork()
{
    Debug_printf("iwmNetwork::iwmNetwork()\n");
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
iwmNetwork::~iwmNetwork()
{
    Debug_printf("iwmNetwork::~iwmNetwork()\n");
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

/** iwm COMMANDS ***************************************************************/

/**
 * net Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void iwmNetwork::open(unsigned short s)
{
    uint8_t _aux1 = iwmnet_recv();
    uint8_t _aux2 = iwmnet_recv();
    string d;

    s--;
    s--;

    memset(response, 0, sizeof(response));
    iwmnet_recv_buffer(response, s);
    iwmnet_recv(); // checksum

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

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
 * iwm Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void iwmNetwork::close()
{
    Debug_printf("iwmNetwork::close()\n");

    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

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
bool iwmNetwork::read_channel(unsigned short num_bytes)
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
 * iwm Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to iwm. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void iwmNetwork::write(uint16_t num_bytes)
{
    Debug_printf("!!! WRITE\n");
    memset(response, 0, sizeof(response));

    iwmnet_recv_buffer(response, num_bytes);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    *transmitBuffer += string((char *)response, num_bytes);
    err = iwmnet_write_channel(num_bytes);
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit iwmnet_error or iwmnet_complete().
 */
bool iwmNetwork::iwmnet_write_channel(unsigned short num_bytes)
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
 * iwm Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to iwm.
 */
void iwmNetwork::status()
{
    NetworkStatus s;
    iwmnet_recv(); // CK
    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

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
void iwmNetwork::get_prefix()
{
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    Debug_printf("iwmNetwork::iwmnet_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();
}

/**
 * Set Prefix
 */
void iwmNetwork::set_prefix(unsigned short s)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    iwmnet_recv_buffer(prefixSpec, s);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("iwmNetwork::iwmnet_set_prefix(%s)\n", prefixSpec_str.c_str());

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
void iwmNetwork::set_login()
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));

    iwmnet_recv_buffer(loginspec, s);
    iwmnet_recv(); // ck

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    login = string((char *)loginspec, s);
}

/**
 * Set password
 */
void iwmNetwork::set_password()
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));

    iwmnet_recv_buffer(passwordspec, s);
    iwmnet_recv(); // ck

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    password = string((char *)passwordspec, s);
}

void iwmNetwork::del()
{
    string d;

    memset(response, 0, sizeof(response));
    iwmnet_recv_buffer(response, s);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

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

void iwmNetwork::rename()
{
    string d;

    memset(response, 0, sizeof(response));
    iwmnet_recv_buffer(response, s);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = ' ';

    if (protocol->perform_idempotent_80(urlParser, &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void iwmNetwork::mkdir()
{
    string d;

    memset(response, 0, sizeof(response));
    iwmnet_recv_buffer(response, s);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    d = string((char *)response, s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = '*';

    if (protocol->perform_idempotent_80(urlParser, &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void iwmNetwork::channel_mode()
{
    switch (iwmnet_recv())
    {
    case 0:
        channelMode = PROTOCOL;
        iwmNet.start_time = esp_timer_get_time();
        iwmnet_response_ack();
        break;
    case 1:
        channelMode = JSON;
        iwmNet.start_time = esp_timer_get_time();
        iwmnet_response_ack();
        break;
    default:
        iwmNet.start_time = esp_timer_get_time();
        iwmnet_response_nack();
        break;
    }
    iwmnet_recv(); // CK
}

void iwmNetwork::json_query(unsigned short s)
{
    uint8_t *c = (uint8_t *)malloc(s);

    iwmnet_recv_buffer(c, s);
    iwmnet_recv(); // CK

    iwmNet.start_time = esp_timer_get_time();
    iwmnet_response_ack();

    json.setReadQuery(std::string((char *)c, s));

    Debug_printf("iwmNetwork::json_query(%s)\n", c);

    free(c);
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: iwm call.
 */
void iwmNetwork::iwmnet_special_inquiry()
{
}

void iwmNetwork::do_inquiry(unsigned char inq_cmd)
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
 * and based on the return, signal iwmnet_complete() or error().
 */
void iwmNetwork::iwmnet_special_00(unsigned short s)
{
    cmdFrame.aux1 = iwmnet_recv();
    cmdFrame.aux2 = iwmnet_recv();

    iwmNet.start_time = esp_timer_get_time();

    if (protocol->special_00(&cmdFrame) == false)
        iwmnet_response_ack();
    else
        iwmnet_response_nack();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void iwmNetwork::iwmnet_special_40(unsigned short s)
{
    cmdFrame.aux1 = iwmnet_recv();
    cmdFrame.aux2 = iwmnet_recv();

    if (protocol->special_40(response, 1024, &cmdFrame) == false)
        iwmnet_response_ack();
    else
        iwmnet_response_nack();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void iwmNetwork::iwmnet_special_80(unsigned short s)
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    cmdFrame.aux1 = iwmnet_recv();
    cmdFrame.aux2 = iwmnet_recv();
    iwmnet_recv_buffer(spData, s);

    Debug_printf("iwmNetwork::iwmnet_special_80() - %s\n", spData);

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
        iwmnet_response_ack();
    else
        iwmnet_response_nack();
}

void iwmNetwork::iwm_open(cmdPacket_t cmd)
{
    Debug_printf("\r\nOpen Network Unit # %02x", cmd.g7byte1);
    encode_status_reply_packet();
    IWM.iwm_send_packet((unsigned char *)packet_buffer);
}

void iwmNetwork::iwm_close(cmdPacket_t cmd)
{
    // Probably need to send close command here.
}

void iwmNetwork::iwm_status(cmdPacket_t cmd)
{
}

void iwmNetwork::iwm_read(cmdPacket_t cmd)
{
}

void iwmNetwork::iwm_ctrl(cmdPacket_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // packet_buffer[6];
    uint8_t control_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // ctrl codes 00-FF
    Debug_printf("\r\nDevice %02x Control Code %02x", source, control_code);
    Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
    IWM.iwm_read_packet_timeout(100, (uint8_t *)packet_buffer, BLOCK_PACKET_LEN);
    Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", packet_buffer[11] & 0x7f, packet_buffer[12] & 0x7f);
    decode_data_packet();
    print_packet((uint8_t *)packet_buffer);

    switch (control_code)
    {
    case ' ':
        rename();
        break;
    case '!':
        del();
        break;
    case '*':
        mkdir();
        break;
    case ',':
        set_prefix();
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
        set_login();
        break;
    case 0xFE: // password
        set_password();
        break;
    default:
        switch (channelMode)
        {
        case PROTOCOL:
            if (inq_dstats == 0x00)
                iwmnet_special_00(s);
            else if (inq_dstats == 0x40)
                iwmnet_special_40(s);
            else if (inq_dstats == 0x80)
                iwmnet_special_80(s);
            else
                Debug_printf("iwmnet_control_send() - Unknown Command: %02x\n", c);
        case JSON:
            switch (control_code)
            {
            case 'P':
                json.parse();
                break;
            case 'Q':
                json_query();
                break;
            }
            break;
        default:
            Debug_printf("Unknown channel mode\n");
            break;
        }
        do_inquiry(control_code);
    }
}

void iwmNetwork::iwmnet_control_send()
{
     = iwmnet_recv_length(); // receive length
    uint8_t c = iwmnet_recv();         // receive command

    s--; // Because we've popped the command off the stack

    switch (c)
    {
    }
}

void iwmNetwork::process(cmdPacket_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case 0x80: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x81: // read block
        iwm_return_badcmd(cmd);
        break;
    case 0x82: // write block
        iwm_return_badcmd(cmd);
        break;
    case 0x83: // format
        iwm_return_badcmd(cmd);
        break;
    case 0x84: // control
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case 0x86: // open
        Debug_printf("\r\nhandling open command");
        iwm_open(cmd);
        break;
    case 0x87: // close
        Debug_printf("\r\nhandling close command");
        iwm_close(cmd);
        break;
    case 0x88: // read
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        break;
    case 0x89: // write
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool iwmNetwork::instantiate_protocol()
{
    if (urlParser == nullptr)
    {
        Debug_printf("iwmNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
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
        Debug_printf("iwmNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    if (!login.empty())
    {
        protocol->login = &login;
        protocol->password = &password;
    }

    Debug_printf("iwmNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}

void iwmNetwork::parse_and_instantiate_protocol(string d)
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
bool iwmNetwork::isValidURL(EdUrlParser *url)
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
 * of iwmNetwork.
 *
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool iwmNetwork::parseURL()
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

    Debug_printf("iwmNetwork::parseURL(%s)\n", deviceSpec.c_str());

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

    Debug_printf("iwmNetwork::parseURL transformed to (%s, %s)\n", deviceSpec.c_str(), url.c_str());

    return isValidURL(urlParser);
}

void iwmNetwork::iwmnet_set_translation()
{
    // trans_aux2 = cmdFrame.aux2;
    // iwmnet_complete();
}

void iwmNetwork::iwmnet_set_timer_rate()
{
    // timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // // Stop extant timer
    // timer_stop();

    // // Restart timer if we're running a protocol.
    // if (protocol != nullptr)
    //     timer_start();

    // iwmnet_complete();
}

#endif /* BUILD_iwm */