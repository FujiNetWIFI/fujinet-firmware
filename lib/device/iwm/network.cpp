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

void iwmNetwork::shutdown()
{
    // TODO: come back here and make shutdown() close all connections.
}

void iwmNetwork::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmNetwork::send_status_dib_reply_packet()
{
    uint8_t data[25];

    //* write data buffer first (25 bytes) 3 grp7 + 4 odds
    // General Status byte
    // Bit 7: Block  device
    // Bit 6: Write allowed
    // Bit 5: Read allowed
    // Bit 4: Device online or disk in drive
    // Bit 3: Format allowed
    // Bit 2: Media write protected (block devices only)
    // Bit 1: Currently interrupting (//c only)
    // Bit 0: Currently open (char devices only)
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0;    // block size 1
    data[2] = 0;    // block size 2
    data[3] = 0;    // block size 3
    data[4] = 0x07; // ID string length - 11 chars
    data[5] = 'N';
    data[6] = 'E';
    data[7] = 'T';
    data[8] = 'W';
    data[9] = 'O';
    data[10] = 'R';
    data[11] = 'K';
    data[12] = ' ';
    data[13] = ' ';
    data[14] = ' ';
    data[15] = ' ';
    data[16] = ' ';
    data[17] = ' ';
    data[18] = ' ';
    data[19] = ' ';
    data[20] = ' ';                             // ID string (16 chars total)
    data[21] = SP_TYPE_BYTE_FUJINET_NETWORK;    // Device type    - 0x02  harddisk
    data[22] = SP_SUBTYPE_BYTE_FUJINET_NETWORK; // Device Subtype - 0x0a
    data[23] = 0x00;                            // Firmware version 2 bytes
    data[24] = 0x01;                            //
    IWM.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 25);
}

/**
 * net Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void iwmNetwork::open()
{
    int idx = 0;
    uint8_t _aux1 = data_buffer[idx++];
    uint8_t _aux2 = data_buffer[idx++];
    string d = string((char *)&data_buffer[idx], 256);

    Debug_printf("aux1: %u aux2: %u path %s", _aux1, _aux2, d.c_str());

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
    Debug_printf("iwmNetwork::get_prefix(%s)\n",prefix.c_str());
    memset(data_buffer,0,sizeof(data_buffer));
    memcpy(data_buffer,prefix.c_str(),prefix.length());
    data_len = prefix.length();
}

/**
 * Set Prefix
 */
void iwmNetwork::set_prefix()
{
    string prefixSpec_str = string((const char *)data_buffer);
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
    login.clear();
    login = string((char *)data_buffer, 256);
    Debug_printf("Login is %s\n",login.c_str());
}

/**
 * Set password
 */
void iwmNetwork::set_password()
{
    password.clear();
    password = string((char *)data_buffer, 256);
    Debug_printf("Password is %s\n",password.c_str());
}

void iwmNetwork::del()
{
    string d;

    d = string((char *)data_buffer, 256);
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

    d = string((char *)data_buffer, 256);
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

    d = string((char *)data_buffer, 256);
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
    switch (data_buffer[0])
    {
    case 0:
        Debug_printf("channelMode = PROTOCOL\n");
        channelMode = PROTOCOL;
        break;
    case 1:
        Debug_printf("channelMode = JSON\n");
        channelMode = JSON;
        break;
    default:
        Debug_printf("INVALID MODE = %02x\r\n", data_buffer[0]);
        break;
    }
}

void iwmNetwork::json_query(iwm_decoded_cmd_t cmd)
{
    // uint8_t source = cmd.dest; // we are the destination and will become the source // data_buffer[6];

    uint16_t numbytes = get_numbytes(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    // numbytes |= ((cmd.g7byte4 & 0x7f) | ((cmd.grp7msb << 4) & 0x80)) << 8;

    uint32_t addy = get_address(cmd); // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);
    // addy |= ((cmd.g7byte6 & 0x7f) | ((cmd.grp7msb << 6) & 0x80)) << 8;
    // addy |= ((cmd.g7byte7 & 0x7f) | ((cmd.grp7msb << 7) & 0x80)) << 16;

    Debug_printf("Query set to: %s\n", string((char *)data_buffer, data_len).c_str());
    json.setReadQuery(string((char *)data_buffer, data_len),cmdFrame.aux2);
}

void iwmNetwork::json_parse()
{
    json.parse();
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
    cmdFrame.aux1 = data_buffer[0];
    cmdFrame.aux2 = data_buffer[1];

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
    cmdFrame.aux1 = data_buffer[0];
    cmdFrame.aux2 = data_buffer[1];

    if (protocol->special_40(data_buffer, 256, &cmdFrame) == false)
    {
        data_len = 256;
        //send_data_packet(data_len);
        IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
    }
    else
    {
        send_reply_packet(SP_ERR_BADCMD);
    }
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
    cmdFrame.aux1 = data_buffer[0];
    cmdFrame.aux2 = data_buffer[1];

    Debug_printf("iwmNetwork::iwmnet_special_80() - %s\n", &data_buffer[2]);

    // Do protocol action and return
    if (protocol->special_80(&data_buffer[2], SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
    {
        // GOOD
    }
    else
    {
        // BAD
    }
}

void iwmNetwork::iwm_open(iwm_decoded_cmd_t cmd)
{
    //Debug_printf("\r\nOpen Network Unit # %02x\n", cmd.g7byte1);
    send_reply_packet(SP_ERR_NOERROR);
}

void iwmNetwork::iwm_close(iwm_decoded_cmd_t cmd)
{
    // Probably need to send close command here.
    //Debug_printf("\r\nClose Network Unit # %02x\n", cmd.g7byte1);
    send_reply_packet(SP_ERR_NOERROR);
    close();
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
        err = json.status(&s);
        break;
    }

    Debug_printf("Bytes Waiting: %u, Connected: %u, Error: %u\n",s.rxBytesWaiting,s.connected,s.error);

    if (s.rxBytesWaiting > 512)
        s.rxBytesWaiting = 512;
    
    data_buffer[0] = s.rxBytesWaiting & 0xFF;
    data_buffer[1] = s.rxBytesWaiting >> 8;
    data_buffer[2] = s.connected;
    data_buffer[3] = s.error;
    data_len = 4;
}

void iwmNetwork::iwm_status(iwm_decoded_cmd_t cmd)
{
    // uint8_t source = cmd.dest;                                                // we are the destination and will become the source // data_buffer[6];
    uint8_t status_code = get_status_code(cmd); //(cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF
    Debug_printf("\r\nDevice %02x Status Code %02x", id(), status_code);
    //Debug_printf("\r\nStatus List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);

    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        send_status_reply_packet();
        return;
        break;
    // case IWM_STATUS_DCB:                  // 0x01
    // case IWM_STATUS_NEWLINE:              // 0x02
    case IWM_STATUS_DIB: // 0x03
        send_status_dib_reply_packet();
        return;
        break;
    case '0':
        get_prefix();
        break;
    case 'R':
        net_read();
        break;
    case 'S':
        status();
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    //send_data_packet(data_len);
    IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
    data_len = 0;
    memset(data_buffer,0,sizeof(data_buffer));
}

void iwmNetwork::net_read()
{
}

bool iwmNetwork::read_channel_json(unsigned short num_bytes, iwm_decoded_cmd_t cmd)
{
    if (num_bytes > json.json_bytes_remaining)
    {
        json.json_bytes_remaining = 0;
        return true;
    }
    else
    {
        json.json_bytes_remaining -= num_bytes;

        json.readValue(data_buffer, num_bytes);
        data_len = json.readValueLen();
    }

    return false;
}

bool iwmNetwork::read_channel(unsigned short num_bytes, iwm_decoded_cmd_t cmd)
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return true; // Punch out.

    // Get status
    protocol->status(&ns);

    if (ns.rxBytesWaiting == 0)
    {
        return true;
    }

    // Truncate bytes waiting to response size
    ns.rxBytesWaiting = (ns.rxBytesWaiting > 512) ? 512 : ns.rxBytesWaiting;
    data_len = ns.rxBytesWaiting;

    if (protocol->read(num_bytes)) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        return true;
    }
    else // everything ok
    {
        statusByte.bits.client_error = 0;
        statusByte.bits.client_data_available = data_len > 0;
        memcpy(data_buffer, receiveBuffer->data(), num_bytes);
        receiveBuffer->erase(0, num_bytes);
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

void iwmNetwork::iwm_read(iwm_decoded_cmd_t cmd)
{
    bool error = false;

    uint16_t numbytes = get_numbytes(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    uint32_t addy = get_address(cmd); // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);

    Debug_printf("\r\nDevice %02x Read %04x bytes from address %06x\n", id(), numbytes, addy);

    data_len = 0;
    memset(data_buffer,0,sizeof(data_buffer));

    switch (channelMode)
    {
    case PROTOCOL:
        error = read_channel(numbytes, cmd);
        break;
    case JSON:
        error = read_channel_json(numbytes, cmd);
        break;
    }

    if (error)
    {
        iwm_return_ioerror();
    }
    else
    {
        Debug_printf("\r\nsending Netwok read data packet (%04x bytes)...", numbytes);
        IWM.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, numbytes);
        data_len = 0;
        memset(data_buffer, 0, sizeof(data_buffer));
    }
}

void iwmNetwork::net_write()
{
    // TODO: Handle errors.
    *transmitBuffer += string((char *)data_buffer, data_len);
    write_channel(data_len);
}

void iwmNetwork::iwm_write(iwm_decoded_cmd_t cmd)
{
    Debug_printf("\r\nNet# %02x ", id());

    uint16_t num_bytes = get_numbytes(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80);
    uint32_t addy = get_address(cmd); // (cmd.g7byte5 & 0x7f) | ((cmd.grp7msb << 5) & 0x80);

    Debug_printf("\nWrite %u bytes to address %04x\n", num_bytes);

    // get write data packet, keep trying until no timeout
    //  to do - this blows up - check handshaking
    data_len = BLOCK_DATA_LEN;
    IWM.iwm_decode_data_packet((unsigned char *)data_buffer, data_len);
    // if (IWM.iwm_decode_data_packet(100, (unsigned char *)data_buffer, data_len)) // write data packet now read in ISR
    // {
    //     Debug_printf("\r\nTIMEOUT in read packet!");
    //     return;
    // }
    // partition number indicates which 32mb block we access
    if (data_len == -1)
        iwm_return_ioerror();
    else
    {
        *transmitBuffer += string((char *)data_buffer, num_bytes);
        if (write_channel(num_bytes))
        {
            send_reply_packet(SP_ERR_IOERROR);
        }
        else
        {
            send_reply_packet(SP_ERR_NOERROR);
        }
    }

    data_len = 0;
    memset(data_buffer,0,sizeof(data_buffer));
}

void iwmNetwork::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    // uint8_t source = cmd.dest;                                                 // we are the destination and will become the source // data_buffer[6];
    uint8_t control_code = get_status_code(cmd); // (cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // ctrl codes 00-FF
    Debug_printf("\r\nNet Device %02x Control Code %02x", id(), control_code);
    // Debug_printf("\r\nControl List is at %02x %02x", cmd.g7byte1 & 0x7f, cmd.g7byte2 & 0x7f);
    data_len = BLOCK_DATA_LEN;
    IWM.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
    // Debug_printf("\r\nThere are %02x Odd Bytes and %02x 7-byte Groups", data_buffer[11] & 0x7f, data_buffer[12] & 0x7f);
    print_packet((uint8_t *)data_buffer);

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
    case 'W':
        net_write();
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
            do_inquiry(control_code);
            if (inq_dstats == 0x00)
                special_00();
            else if (inq_dstats == 0x40) // MOVE THIS TO STATUS!
                special_40();
            else if (inq_dstats == 0x80)
                special_80();
            else
                Debug_printf("iwmnet_control_send() - Unknown Command: %02x\n", control_code);
            break;
        case JSON:
            switch (control_code)
            {
            case 'P':
                json_parse();
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
    }

    if (statusByte.bits.client_error == true)
        err_result = SP_ERR_IOERROR;

    send_reply_packet(err_result);

    data_len = 0;
    memset(data_buffer,0,sizeof(data_buffer));
}

void iwmNetwork::process(iwm_decoded_cmd_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case 0x00: // status
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case 0x01: // read block
        iwm_return_badcmd(cmd);
        break;
    case 0x02: // write block
        iwm_return_badcmd(cmd);
        break;
    case 0x03: // format
        iwm_return_badcmd(cmd);
        break;
    case 0x04: // control
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case 0x06: // open
        Debug_printf("\r\nhandling open command");
        iwm_open(cmd);
        break;
    case 0x07: // close
        Debug_printf("\r\nhandling close command");
        iwm_close(cmd);
        break;
    case 0x08: // read
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        break;
    case 0x09: // write
        iwm_write(cmd);
        break;
    default:
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