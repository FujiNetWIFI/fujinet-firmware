#ifdef BUILD_S100

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

#include "ProtocolParser.h"

//using namespace std;

/**
 * Constructor
 */
s100spiNetwork::s100spiNetwork()
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
}

/**
 * Destructor
 */
s100spiNetwork::~s100spiNetwork()
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

/** s100spi COMMANDS ***************************************************************/

/**
 * s100spi Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void s100spiNetwork::open(unsigned short s)
{
    uint8_t _aux1 = s100spi_recv();
    uint8_t _aux2 = s100spi_recv();
    string d;

    s--; s--;

    memset(response,0,sizeof(response));
    s100spi_recv_buffer(response, s);
    s100spi_recv(); // checksum


    s100spi_response_ack();

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
    d=string((char *)response,s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), &cmdFrame) == true)
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
}

/**
 * s100spi Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void s100spiNetwork::close()
{
    Debug_printf("s100spiNetwork::close()\n");

    s100spi_recv(); // CK


    s100spi_response_ack();

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
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool s100spiNetwork::read_channel(unsigned short num_bytes)
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
 * s100spi Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to s100spi. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void s100spiNetwork::write(uint16_t num_bytes)
{
    memset(response, 0, sizeof(response));

    s100spi_recv_buffer(response, num_bytes);
    s100spi_recv(); // CK


    s100spi_response_ack();

    *transmitBuffer += string((char *)response, num_bytes);
    err = s100spiNetwork_write_channel(num_bytes);
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit s100spi_error or s100spi_complete().
 */
bool s100spiNetwork::s100spiNetwork_write_channel(unsigned short num_bytes)
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
 * s100spi Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to s100spi.
 */
void s100spiNetwork::status()
{
    NetworkStatus s;
    s100spi_recv(); // CK

    s100spi_response_ack();

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = true;
            s.error = true;
        } else {
            err = protocol->status(&s);
        }
        break;
    case JSON:
        // err = _json->status(&status);
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
void s100spiNetwork::get_prefix()
{
    s100spi_recv(); // CK


    s100spi_response_ack();

    Debug_printf("s100spiNetwork::s100spi_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();
}

/**
 * Set Prefix
 */
void s100spiNetwork::set_prefix(unsigned short s)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    s100spi_recv_buffer(prefixSpec, s);
    s100spi_recv(); // CK


    s100spi_response_ack();

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("s100spiNetwork::s100spi_set_prefix(%s)\n", prefixSpec_str.c_str());

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
}

/**
 * Set login
 */
void s100spiNetwork::set_login(uint16_t s)
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));

    s100spi_recv_buffer(loginspec, s);
    s100spi_recv(); // ck


    s100spi_response_ack();

    login = string((char *)loginspec, s);
}

/**
 * Set password
 */
void s100spiNetwork::set_password(uint16_t s)
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));

    s100spi_recv_buffer(passwordspec, s);
    s100spi_recv(); // ck


    s100spi_response_ack();

    password = string((char *)passwordspec, s);
}

void s100spiNetwork::del(uint16_t s)
{
    string d;

    memset(response,0,sizeof(response));
    s100spi_recv_buffer(response, s);
    s100spi_recv(); // CK


    s100spi_response_ack();

    d=string((char *)response,s);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
        return;

    cmdFrame.comnd = '!';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void s100spiNetwork::rename(uint16_t s)
{
    string d;

    memset(response,0,sizeof(response));
    s100spi_recv_buffer(response, s);
    s100spi_recv(); // CK


    s100spi_response_ack();

    d=string((char *)response,s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = ' ';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

void s100spiNetwork::mkdir(uint16_t s)
{
    string d;

    memset(response,0,sizeof(response));
    s100spi_recv_buffer(response,s);
    s100spi_recv(); // CK


    s100spi_response_ack();

    d=string((char *)response,s);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = '*';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        return;
    }
}

/**
 * s100spi Special, called as a default for any other s100spi command not processed by the other s100spi_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either s100spi_complete()
 * or s100spi_error() is called.
 */
void s100spiNetwork::s100spiNetwork_special()
{
    // do_inquiry(cmdFrame.comnd);

    // switch (inq_dstats)
    // {
    // case 0x00: // No payload
    //     s100spi_ack();
    //     s100spi_special_00();
    //     break;
    // case 0x40: // Payload to Atari
    //     s100spi_ack();
    //     s100spi_special_40();
    //     break;
    // case 0x80: // Payload to Peripheral
    //     s100spi_ack();
    //     s100spi_special_80();
    //     break;
    // default:
    //     s100spi_nak();
    //     break;
    // }
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: s100spi call.
 */
void s100spiNetwork::s100spiNetwork_special_inquiry()
{
    // // Acknowledge
    // s100spi_ack();

    // Debug_printf("s100spiNetwork::s100spi_special_inquiry(%02x)\n", cmdFrame.aux1);

    // do_inquiry(cmdFrame.aux1);

    // // Finally, return the completed inq_dstats value back to Atari
    // bus_to_computer(&inq_dstats, sizeof(inq_dstats), false); // never errors.
}

void s100spiNetwork::do_inquiry(fujiCommandID_t inq_cmd)
{
    // // Reset inq_dstats
    // inq_dstats = 0xff;

    // // Ask protocol for dstats, otherwise get it locally.
    // if (protocol != nullptr)
    //     inq_dstats = protocol->special_inquiry(inq_cmd);

    // // If we didn't get one from protocol, or unsupported, see if supported globally.
    // if (inq_dstats == 0xFF)
    // {
    //     switch (inq_cmd)
    //     {
    //     case 0x20:
    //     case 0x21:
    //     case 0x23:
    //     case 0x24:
    //     case 0x2A:
    //     case 0x2B:
    //     case 0x2C:
    //     case 0xFD:
    //     case 0xFE:
    //         inq_dstats = 0x80;
    //         break;
    //     case 0x30:
    //         inq_dstats = 0x40;
    //         break;
    //     case 'Z': // Set interrupt rate
    //         inq_dstats = 0x00;
    //         break;
    //     case 'T': // Set Translation
    //         inq_dstats = 0x00;
    //         break;
    //     case 0x80: // JSON Parse
    //         inq_dstats = 0x00;
    //         break;
    //     case 0x81: // JSON Query
    //         inq_dstats = 0x80;
    //         break;
    //     default:
    //         inq_dstats = 0xFF; // not supported
    //         break;
    //     }
    // }

    // Debug_printf("inq_dstats = %u\n", inq_dstats);
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal s100spi_complete() or error().
 */
void s100spiNetwork::s100spiNetwork_special_00()
{
    // // Handle commands that exist outside of an open channel.
    // switch (cmdFrame.comnd)
    // {
    // case 'T':
    //     s100spi_set_translation();
    //     break;
    // case 'Z':
    //     s100spi_set_timer_rate();
    //     break;
    // default:
    //     if (protocol->special_00(&cmdFrame) == false)
    //         s100spi_complete();
    //     else
    //         s100spi_error();
    // }
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void s100spiNetwork::s100spiNetwork_special_40()
{
    // // Handle commands that exist outside of an open channel.
    // switch (cmdFrame.comnd)
    // {
    // case 0x30:
    //     s100spi_get_prefix();
    //     return;
    // }

    // bus_to_computer((uint8_t *)receiveBuffer->data(),
    //                 SPECIAL_BUFFER_SIZE,
    //                 protocol->special_40((uint8_t *)receiveBuffer->data(), SPECIAL_BUFFER_SIZE, &cmdFrame));
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void s100spiNetwork::s100spiNetwork_special_80()
{
    // uint8_t spData[SPECIAL_BUFFER_SIZE];

    // // Handle commands that exist outside of an open channel.
    // switch (cmdFrame.comnd)
    // {
    // case 0x20: // RENAME
    // case 0x21: // DELETE
    // case 0x23: // LOCK
    // case 0x24: // UNLOCK
    // case 0x2A: // MKDIR
    // case 0x2B: // RMDIR
    //     s100spi_do_idempotent_command_80();
    //     return;
    // case 0x2C: // CHDIR
    //     s100spi_set_prefix();
    //     return;
    // case 0xFD: // LOGIN
    //     s100spi_set_login();
    //     return;
    // case 0xFE: // PASSWORD
    //     s100spi_set_password();
    //     return;
    // }

    // memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // // Get special (devicespec) from computer
    // bus_to_peripheral(spData, SPECIAL_BUFFER_SIZE);

    // Debug_printf("s100spiNetwork::s100spi_special_80() - %s\n", spData);

    // // Do protocol action and return
    // if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
    //     s100spi_complete();
    // else
    //     s100spi_error();
}

void s100spiNetwork::s100spi_response_status()
{
    NetworkStatus s;

    if (protocol != nullptr)
        protocol->status(&s);

    statusByte.bits.client_connected = s.connected == true;
    statusByte.bits.client_data_available = s.rxBytesWaiting > 0;
    statusByte.bits.client_error = s.error > 1;

    status_response[4] = statusByte.byte;
    virtualDevice::s100spi_response_status();
}

void s100spiNetwork::s100spi_control_ack()
{
}

void s100spiNetwork::s100spi_control_send()
{
    uint16_t s = s100spi_recv_length(); // receive length
    uint8_t c = s100spi_recv();        // receive command

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
    case 0xFD: // login
        set_login(s);
        break;
    case 0xFE: // password
        set_password(s);
        break;
    default:
        Debug_printf("s100spi_control_send() - Unknown Command: %02x\n", c);
    }
}

void s100spiNetwork::s100spi_control_clr()
{
    s100spi_response_send();
}

void s100spiNetwork::s100spi_control_receive_channel()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    // Get status
    protocol->status(&ns);

    if (ns.rxBytesWaiting > 0)
        s100spi_response_ack();
    else
    {
        s100spi_response_nack();
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
        for (int i = 0; i < response_len; i++)
        {
            Debug_printf("%c", response[i]);
        }
        receiveBuffer->erase(0, response_len);
    }
}

void s100spiNetwork::s100spi_control_receive()
{


    if (response_len > 0) // There is response data, go ahead and ack.
    {
        s100spi_response_ack();
        return;
    }
    else if (protocol == nullptr)
    {
        s100spi_response_nack();
        return;
    }

    switch (receiveMode)
    {
    case CHANNEL:
        s100spi_control_receive_channel();
        break;
    case STATUS:
        break;
    }
}

void s100spiNetwork::s100spi_response_send()
{
    uint8_t c = s100spi_checksum(response, response_len);

    s100spi_send(0xB0 | _devnum);
    s100spi_send_length(response_len);
    s100spi_send_buffer(response, response_len);
    s100spi_send(c);

    memset(response, 0, response_len);
    response_len = 0;
}

/**
 * Process incoming s100spi command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void s100spiNetwork::s100spi_process(uint8_t b)
{
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool s100spiNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("s100spiNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("s100spiNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void s100spiNetwork::create_devicespec(string d)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, cmdFrame.aux1 == 6, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void s100spiNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void s100spiNetwork::parse_and_instantiate_protocol(string d)
{
    create_devicespec(d);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_INVALID_DEVICESPEC;
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
        err = NETWORK_ERROR_GENERAL;
        return;
    }
}

void s100spiNetwork::s100spiNetwork_set_translation()
{
}

void s100spiNetwork::s100spiNetwork_set_timer_rate()
{
}

void s100spiNetwork::s100spiNetwork_do_idempotent_command_80()
{
}

#endif /* NEW_TARGET */
