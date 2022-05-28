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
void iwmNetwork::open()
{
    uint16_t idx = 0;
    uint8_t _aux1 = packet_buffer[idx++];
    uint8_t _aux2 = packet_buffer[idx++];
    string d = string((char *)&packet_buffer[2], 256);

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
    d = string((char *)response, 256);
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
 * Get Prefix
 */
void iwmNetwork::get_prefix()
{
    // RE-implement
}

/**
 * Set Prefix
 */
void iwmNetwork::set_prefix()
{
    string prefixSpec_str = string((const char *)packet_buffer);
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
    login = string((char *)packet_buffer, 256);
}

/**
 * Set password
 */
void iwmNetwork::set_password()
{
    password = string((char *)packet_buffer, 256);
}

void iwmNetwork::del()
{
    string d;

    d = string((char *)packet_buffer, 256);
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

    d = string((char *)packet_buffer, 256);
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

    d = string((char *)packet_buffer, 256);
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
    switch (packet_buffer[1])
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
}

void iwmNetwork::json_query(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];

    uint16_t numbytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    json.setReadQuery(string((char *)packet_buffer, numbytes));
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
void iwmNetwork::special_00()
{
    cmdFrame.aux1 = packet_buffer[0];
    cmdFrame.aux2 = packet_buffer[1];

    protocol->special_00(&cmdFrame);
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void iwmNetwork::special_40()
{
    cmdFrame.aux1 = packet_buffer[0];
    cmdFrame.aux2 = packet_buffer[1];

    if (protocol->special_40(packet_buffer, 256, &cmdFrame) == false)
    {
        packet_len = 256;
        encode_data_packet(packet_len);
    }
    else
    {
        encode_error_reply_packet(SP_ERR_BADCMD);
    }

    IWM.iwm_send_packet((uint8_t *)packet_buffer);
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void iwmNetwork::special_80()
{
    // Get special (devicespec) from computer
    cmdFrame.aux1 = packet_buffer[0];
    cmdFrame.aux2 = packet_buffer[1];

    Debug_printf("iwmNetwork::iwmnet_special_80() - %s\n", &packet_buffer[2]);

    // Do protocol action and return
    if (protocol->special_80(&packet_buffer[2], SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
    {
        // GOOD
    }
    else
    {
        // BAD
    }
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

void iwmNetwork::status()
{
    NetworkStatus s;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->status(&s);
        break;
    case JSON:
        // err = json.status(&status);
        break;
    }

    packet_buffer[0] = s.rxBytesWaiting & 0xFF;
    packet_buffer[1] = s.rxBytesWaiting >> 8;
    packet_buffer[2] = s.connected;
    packet_buffer[3] = s.error;
    packet_len = 4;

    encode_data_packet(packet_len);
    IWM.iwm_send_packet((uint8_t *)packet_buffer);
}

void iwmNetwork::iwm_status(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest;                                                // we are the destination and will become the source // packet_buffer[6];
    uint8_t status_code = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\nDevice %02x Status Code %02x", source, status_code);
    Debug_printf("\r\nStatus List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    switch (status_code)
    {
    case IWM_STATUS_STATUS:
        status();
        break;
    }
}

bool iwmNetwork::read_channel(unsigned short num_bytes, cmdPacket_t cmd)
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return true; // Punch out.

    // Get status
    protocol->status(&ns);

    if (ns.rxBytesWaiting == 0)
    {
        iwm_return_ioerror(cmd);
        return true;
    }

    // Truncate bytes waiting to response size
    ns.rxBytesWaiting = (ns.rxBytesWaiting > 1024) ? 1024 : ns.rxBytesWaiting;
    response_len = ns.rxBytesWaiting;

    if (protocol->read(response_len)) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        return true;
    }
    else // everything ok
    {
        statusByte.bits.client_error = 0;
        statusByte.bits.client_data_available = response_len > 0;
        memcpy(response, receiveBuffer->data(), response_len);
        receiveBuffer->erase(0, response_len);
    }
    return false;
}

bool iwmNetwork::write_channel(unsigned short num_bytes)
{
    switch (channelMode)
    {
    case PROTOCOL:
        protocol->write(num_bytes);
    case JSON:
        break;
    }
    return false;
}

void iwmNetwork::iwm_read(cmdPacket_t cmd)
{
    uint8_t source = cmd.dest; // we are the destination and will become the source // packet_buffer[6];

    uint16_t numbytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x", source, numbytes, addy);

    switch (channelMode)
    {
    case PROTOCOL:
        read_channel(numbytes, cmd);
        break;
    case JSON:
        break;
    }
}

void iwmNetwork::iwm_write(cmdPacket_t cmd)
{
    uint8_t status = 0;
    uint8_t source = cmd.dest; // packet_buffer[6];
    // to do - actually we will already know that the cmd.dest == id(), so can just use id() here
    Debug_printf("\r\nNet# %02x ", source);

    uint16_t num_bytes = (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    num_bytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    Debug_printf("\nWrite %u bytes to address %04x\n", num_bytes);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking
    if (IWM.iwm_read_packet_timeout(100, (unsigned char *)packet_buffer, BLOCK_PACKET_LEN))
    {
        Debug_printf("\r\nTIMEOUT in read packet!");
        return;
    }
    // partition number indicates which 32mb block we access
    if (decode_data_packet())
        iwm_return_ioerror(cmd);
    else
    {
        *transmitBuffer += string((char *)response, num_bytes);
        if (write_channel(num_bytes))
        {
            encode_error_reply_packet(SP_ERR_IOERROR);
            IWM.iwm_send_packet((uint8_t *)packet_buffer);
        }
        else
        {
            encode_write_status_packet(source, 0);
            IWM.iwm_send_packet((uint8_t *)packet_buffer);
        }
    }
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
        open();
        break;
    case 'C':
        close();
        break;
    case 'S':
        status();
        break;
    case 'W':
        write();
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
                special_00();
            else if (inq_dstats == 0x40) // MOVE THIS TO STATUS!
                special_40();
            else if (inq_dstats == 0x80)
                special_80();
            else
                Debug_printf("iwmnet_control_send() - Unknown Command: %02x\n", control_code);
        case JSON:
            switch (control_code)
            {
            case 'P':
                json.parse();
                break;
            case 'Q':
                json_query(cmd);
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