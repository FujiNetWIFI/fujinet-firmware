#ifdef BUILD_RS232

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"
#include "fuji_endian.h"
#include "fujiCommandID.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
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

#define DEFAULT_LINE_ENDING "\n"

using namespace std;

#ifdef ESP_PLATFORM
/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
 * flag to true. This is set to false when the interrupt is serviced.
 */
void onTimer(void *info)
{
    rs232Network *parent = (rs232Network *)info;
    portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptProceed = !parent->interruptProceed;
    portEXIT_CRITICAL_ISR(&parent->timerMux);
}
#endif /* ESP_PLATFORM */

/**
 * Constructor
 */
rs232Network::rs232Network()
{
    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    json.setLineEnding(DEFAULT_LINE_ENDING);
}

/**
 * Destructor
 */
rs232Network::~rs232Network()
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

/** RS232 COMMANDS ***************************************************************/

/**
 * RS232 Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void rs232Network::rs232_open(fileAccessMode_t access, netProtoTranslation_t translate)
{
    Debug_println("rs232Network::rs232_open()\n");

    rs232_ack();

    channelMode = CHANNEL_MODE::PROTOCOL;

#ifdef ESP_PLATFORM
    // Delete timer if already extant.
    timer_stop();
#endif /* ESP_PLATFORM */

    trans_mode = translate;

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
    status.reset();

    // Parse and instantiate protocol
    parse_and_instantiate_protocol(access);

    if (protocol == nullptr)
    {
        // invalid devicespec error already passed in.
        rs232_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), access, translate) != PROTOCOL_ERROR::NONE)
    {
        status.error = protocol->error;
        Debug_printf("Protocol unable to make connection. Error: %d\n", (int) status.error);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        rs232_error();
        return;
    }

#ifdef ESP_PLATFORM
    // Everything good, start the interrupt timer!
    timer_start();
#endif

    // Go ahead and send an interrupt, so Atari knows to get status.
    rs232_assert_interrupt();

    // TODO: Finally, go ahead and let the parsers know
    json.setProtocol(protocol);
    json.setLineEnding(DEFAULT_LINE_ENDING);
    protocol->setLineEnding(DEFAULT_LINE_ENDING);
    channelMode = CHANNEL_MODE::PROTOCOL;

    // And signal complete!
    rs232_complete();
}

/**
 * RS232 Close command
 * Tear down everything set up by rs232_open(), as well as RX interrupt.
 */
void rs232Network::rs232_close()
{
    Debug_printf("rs232Network::rs232_close()\n");

    rs232_ack();

    status.reset();

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }
    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        rs232_complete();
        return;
    }

    // Ask the protocol to close
    if (protocol->close() != PROTOCOL_ERROR::NONE)
        rs232_error();
    else
        rs232_complete();

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;
}

/**
 * RS232 Read command
 * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
 * fill the rest with nulls and return ERROR.
 *
 * @note It is the channel's responsibility to pad to required length.
 */
void rs232Network::rs232_read(uint16_t length)
{
    protocolError_t err = PROTOCOL_ERROR::NONE;

    Debug_printf("rs232Network::rs232_read( %d bytes)\n", length);

    rs232_ack();

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        status.error = NDEV_STATUS::COULD_NOT_ALLOCATE_BUFFERS;
        rs232_error();
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        status.error = NDEV_STATUS::NOT_CONNECTED;
        rs232_error();
        return;
    }

    // Do the channel read
    err = rs232_read_channel(length);

    // And send off to the computer
    bus_to_computer((uint8_t *)receiveBuffer->data(), length, err != PROTOCOL_ERROR::NONE);
    receiveBuffer->erase(0, length);
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
protocolError_t rs232Network::rs232_read_channel_json(uint16_t num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining=0;
    else
        json_bytes_remaining-=num_bytes;

    return PROTOCOL_ERROR::NONE;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
protocolError_t rs232Network::rs232_read_channel(uint16_t num_bytes)
{
    protocolError_t err = PROTOCOL_ERROR::NONE;

    switch (channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        err = protocol->read(num_bytes);
        break;
    case CHANNEL_MODE::JSON:
        err = rs232_read_channel_json(num_bytes);
        break;
    }
    return err;
}

/**
 * RS232 Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to RS232. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void rs232Network::rs232_write(uint16_t length)
{
    uint8_t *newData;
    protocolError_t err = PROTOCOL_ERROR::NONE;

    newData = (uint8_t *)malloc(length);
    Debug_printf("rs232Network::rs232_write( %d bytes)\n", length);

    if (newData == nullptr)
    {
        Debug_printf("Could not allocate %u bytes.\n", length);
        rs232_error();
        return;
    }

    rs232_ack();

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        status.error = NDEV_STATUS::NOT_CONNECTED;
        rs232_error();
        free(newData);
        return;
    }

    // Get the data from the Atari
    bus_to_peripheral(newData, length);
    *transmitBuffer += string((char *)newData, length);
    free(newData);

    // Do the channel write
    err = rs232_write_channel(length);

    // Acknowledge to Atari of channel outcome.
    if (err == PROTOCOL_ERROR::NONE)
    {
        rs232_complete();
    }
    else
        rs232_error();
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit rs232_error or rs232_complete().
 */
protocolError_t rs232Network::rs232_write_channel(uint16_t num_bytes)
{
    protocolError_t err = PROTOCOL_ERROR::NONE;

    switch (channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        err = protocol->write(num_bytes);
        break;
    case CHANNEL_MODE::JSON:
        Debug_printf("JSON Not Handled.\n");
        err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }
    return err;
}

/**
 * RS232 Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to RS232.
 */
void rs232Network::rs232_status(FujiStatusReq reqType) // was aux2
{
    // Acknowledge
    rs232_ack();

    if (protocol == nullptr)
        rs232_status_local(reqType);
    else
        rs232_status_channel();
}

/**
 * @brief perform local status commands, if protocol is not bound, based on cmdFrame
 * value.
 */
void rs232Network::rs232_status_local(FujiStatusReq reqType)
{
    uint8_t ipAddress[4];
    uint8_t ipNetmask[4];
    uint8_t ipGateway[4];
    uint8_t ipDNS[4];
    uint8_t default_status[4] = {0, 0, 0, 0};

    Debug_printf("rs232Network::rs232_status_local(%u)\n", reqType);

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (reqType)
    {
    case 1: // IP Address
        Debug_printf("IP Address: %u.%u.%u.%u\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
        bus_to_computer(ipAddress, 4, false);
        break;
    case 2: // Netmask
        Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
        bus_to_computer(ipNetmask, 4, false);
        break;
    case 3: // Gateway
        Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
        bus_to_computer(ipGateway, 4, false);
        break;
    case 4: // DNS
        Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
        bus_to_computer(ipDNS, 4, false);
        break;
    default:
        default_status[2] = status.connected;
        default_status[3] = (uint8_t) status.error;
        bus_to_computer(default_status, 4, false);
    }
}

protocolError_t rs232Network::rs232_status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? NDEV_STATUS::SUCCESS : NDEV_STATUS::END_OF_FILE;
    return PROTOCOL_ERROR::NONE; // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void rs232Network::rs232_status_channel()
{
    NDeviceStatus nstatus;
    size_t avail = 0;
    protocolError_t err = PROTOCOL_ERROR::NONE;

    Debug_printf("rs232Network::rs232_status_channel(%u)\n", (unsigned) channelMode);

    switch (channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling rs232_status_channel on a null protocol.\r\n");
            err = PROTOCOL_ERROR::UNSPECIFIED;
            status.error = NDEV_STATUS::NOT_CONNECTED;
        } else {
            err = protocol->status(&status);
            avail = protocol->available();
        }
        break;
    case CHANNEL_MODE::JSON:
        rs232_status_channel_json(&status);
        avail = json_bytes_remaining;
        break;
    }

    // Serialize status into status bytes
    avail = avail > 65535 ? 65535 : avail;
    nstatus.avail = htole16(avail);
    nstatus.conn = status.connected;
    nstatus.err = status.error;

    Debug_printf("rs232_status_channel() - BW: %u C: %u E: %u\n",
                 nstatus.avail, nstatus.conn, (uint8_t) nstatus.err);

    // and send to computer
    bus_to_computer((uint8_t *) &nstatus, sizeof(nstatus), err != PROTOCOL_ERROR::NONE);
}

/**
 * Get Prefix
 */
void rs232Network::rs232_get_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    rs232_ack();
    memcpy(prefixSpec, prefix.data(), prefix.size());

    prefixSpec[prefix.size()] = 0x9B; // add EOL.

    bus_to_computer(prefixSpec, sizeof(prefixSpec), false);
}

/**
 * Set Prefix
 */
void rs232Network::rs232_set_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    rs232_ack();
    bus_to_peripheral(prefixSpec, sizeof(prefixSpec)); // TODO test checksum
    util_devicespec_fix_9b(prefixSpec, sizeof(prefixSpec));

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_set_prefix(%s)\n", prefixSpec_str.c_str());
#endif

    // If "NCD Nn:" then prefix is cleared completely
    if (prefixSpec_str.empty())
    {
        prefix.clear();
    }
    else
    {
        // Append trailing slash if not found
        if (prefixSpec_str.back() != '/')
        {
            prefixSpec_str += "/";
        }

        // For the remaining cases, append trailing slash if not found
        if (prefix.back() != '/')
        {
            prefix += "/";
        }

        // Find pos of 3rd "/" in prefix
        size_t pos = prefix.find("/");
        pos = prefix.find("/",++pos);
        pos = prefix.find("/",++pos);

        // If "NCD Nn:.."" or "NCD .." then devance prefix
        if (prefixSpec_str == ".." || prefixSpec_str == "<")
        {
            prefix += ".."; // call to canonical path later will resolve
        }
        // If "NCD Nn:/" or "NCD /" then truncate to hostname (e.g. TNFS://hostname/)
        else if (prefixSpec_str == "/" || prefixSpec_str == ">")
        {
            // truncate at pos of 3rd slash
            prefix = prefix.substr(0,pos+1);
        }
        // If "NCD Nn:/path/to/dir/" then concatenate hostname and prefix
        else if (prefixSpec_str[0] == '/') // N:/DIR
        {
            // append at pos of 3rd slash
            prefix = prefix.substr(0,pos);
            prefix += prefixSpec_str;
        }
        // If "NCD TNFS://foo.com/" then reset entire prefix
        else if (prefixSpec_str.find_first_of(":") != string::npos)
        {
            prefix = prefixSpec_str;
        }
        else // append to path.
        {
            prefix += prefixSpec_str;
        }
    }

    prefix = util_get_canonical_path(prefix);
#ifdef VERBOSE_PROTOCOL
    Debug_printf("Prefix now: %s\n", prefix.c_str());
#endif

    // We are okay, signal complete.
    rs232_complete();
}

/**
 * @brief set channel mode
 */
void rs232Network::rs232_set_channel_mode(channelMode_t newMode) // was aux2
{
    switch (newMode)
    {
    case CHANNEL_MODE::PROTOCOL:
    case CHANNEL_MODE::JSON:
        channelMode = newMode;
        rs232_complete();
        break;
    default:
        rs232_error();
    }
}

/**
 * Set login
 */
void rs232Network::rs232_set_login()
{
    uint8_t loginSpec[256];

    rs232_ack();
    bus_to_peripheral(loginSpec, sizeof(loginSpec));
    util_devicespec_fix_9b(loginSpec, sizeof(loginSpec));

    login = string((char *)loginSpec);
    rs232_complete();
}

/**
 * Set password
 */
void rs232Network::rs232_set_password()
{
    uint8_t passwordSpec[256];

    rs232_ack();
    bus_to_peripheral(passwordSpec, sizeof(passwordSpec));
    util_devicespec_fix_9b(passwordSpec, sizeof(passwordSpec));

    password = string((char *)passwordSpec);
    rs232_complete();
}

void rs232Network::process_tcp(FujiBusPacket &packet)
{
    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol);
    if (!tcp)
    {
        rs232_nak();
        return;
    }

    protocolError_t err;
    switch (packet.command())
    {
    case NETCMD_CONTROL:
        rs232_ack();
        err = tcp->accept_connection();
        break;
    case NETCMD_CLOSE_CLIENT:
        rs232_ack();
        err = tcp->close_client_connection();
        break;
    default:
        rs232_nak();
        return;
    }

    if (err != PROTOCOL_ERROR::NONE)
        rs232_error();
    else
        rs232_complete();
}

void rs232Network::process_http(FujiBusPacket &packet)
{
    // Make sure this is really an HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol);
    if (!http)
    {
        rs232_nak();
        return;
    }

    protocolError_t err;
    switch (packet.command())
    {
    case NETCMD_UNLISTEN:
        rs232_ack();
        err = http->set_channel_mode((netProtoHTTPChannelMode_t) packet.param(1));
        break;
    default:
        rs232_nak();
        return;
    }

    if (err != PROTOCOL_ERROR::NONE)
        rs232_error();
    else
        rs232_complete();
}

void rs232Network::process_udp(FujiBusPacket &packet)
{
    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol);
    if (!udp)
    {
        rs232_nak();
        return;
    }

    protocolError_t err;
    switch (packet.command())
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        rs232_ack();
        err = udp->get_remote(receiveBuffer->data(), SPECIAL_BUFFER_SIZE);
        bus_to_computer((uint8_t *)receiveBuffer->data(), SPECIAL_BUFFER_SIZE, err != PROTOCOL_ERROR::NONE);
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            uint8_t spData[SPECIAL_BUFFER_SIZE];
            bus_to_peripheral(spData, sizeof(spData));
            err = udp->set_destination(spData, sizeof(spData));
            if (err != PROTOCOL_ERROR::NONE)
                rs232_error();
            else
                rs232_complete();
        }
        break;
    default:
        rs232_nak();
        return;
    }
}

void rs232Network::rs232_seek(uint32_t offset)
{
    rs232_ack();
    protocol->seek(offset, SEEK_SET);
    rs232_complete();
    return;
}

void rs232Network::rs232_tell()
{
    off_t offset;
    uint32_t retval;


    // Acknowledge
    rs232_ack();

    offset = protocol->seek(0, SEEK_CUR);
    if (offset == -1) {
        status.error = NDEV_STATUS::SERVER_GENERAL;
        rs232_error();
        return;
    }

    retval = htole32(offset);
    bus_to_computer((unsigned char *) &retval, 4, false);
    return;
}

/**
 * Process incoming RS232 command for device 0x7X
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void rs232Network::rs232_process(FujiBusPacket &packet)
{
    switch (packet.command())
    {
    // Channel-based I/O
    case NETCMD_OPEN:
        if (packet.paramCount() < 2) {
            Debug_printv("Insufficient open paramaters: %d", packet.paramCount());
            rs232_nak();
        }
        else
            rs232_open((fileAccessMode_t) packet.param(0),
                       (netProtoTranslation_t) packet.param(1));
        break;
    case NETCMD_CLOSE:
        rs232_close();
        break;
    case NETCMD_READ:
        rs232_read(packet.param(0));
        break;
    case NETCMD_WRITE:
        rs232_write(packet.param(0));
        break;
    case NETCMD_STATUS:
        {
            FujiStatusReq reqType = STATUS_NETWORK_CONNERR;
            if (packet.paramCount() >= 2)
                reqType = (FujiStatusReq) packet.param(1);
            rs232_status(reqType);
        }
        break;
    case NETCMD_PARSE:
        rs232_ack();
        rs232_parse_json();
        break;
    case NETCMD_QUERY:
        rs232_ack();
        rs232_set_json_query();
        break;
    case NETCMD_CHANNEL_MODE:
        rs232_ack();
        rs232_set_channel_mode((channelMode_t) packet.param(1));
        break;
    case NETCMD_SEEK:
        rs232_seek(packet.param(0));
        break;
    case NETCMD_TELL:
        rs232_tell();
        break;
    case NETCMD_TRANSLATION:
        rs232_set_translation((netProtoTranslation_t) packet.param(1));
        break;
    case NETCMD_SET_INT_RATE:
        rs232_set_timer_rate(packet.param(1));
        break;
    case NETCMD_GETCWD:
        rs232_get_prefix();
        break;
    case NETCMD_CHDIR:
        rs232_set_prefix();
        break;
    case NETCMD_USERNAME:
        rs232_set_login();
        break;
    case NETCMD_PASSWORD:
        rs232_set_password();
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp(packet);
        break;

    case NETCMD_UNLISTEN:
        process_http(packet);
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp(packet);
        break;

    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        process_fs(packet);
        break;

    default:
        rs232_nak();
        break;
    }
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed.
 */
void rs232Network::rs232_poll_interrupt()
{
    if (protocol != nullptr)
    {
        if (protocol->interruptEnable == false)
            return;

        protocol->fromInterrupt = true;
        protocol->status(&status);
        protocol->fromInterrupt = false;

        if (protocol->available() > 0 || status.connected == 0)
            rs232_assert_interrupt();
#ifdef ESP_PLATFORM
        else
            fnSystem.digital_write(PIN_RS232_RI,DIGI_HIGH);
#endif /* ESP_PLATFORM */

        reservedSave = status.connected;
        errorSave = status.error;
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool rs232Network::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("rs232Network::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("rs232Network::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void rs232Network::create_devicespec(fileAccessMode_t access)
{
    // Clean up devicespec buffer.
    memset(devicespecBuf, 0, sizeof(devicespecBuf));

    // Get Devicespec from buffer, and put into primary devicespec string
    bus_to_peripheral(devicespecBuf, sizeof(devicespecBuf));
    util_devicespec_fix_9b(devicespecBuf, sizeof(devicespecBuf));
    deviceSpec = string((char *)devicespecBuf);

    /* Clear Prefix if a full URL with Protocol is specified. */
    if (deviceSpec.find("://") != string::npos)
    {
        prefix.clear();
    }

    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix,
                                                 access == ACCESS_MODE::DIRECTORY, true);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void rs232Network::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void rs232Network::parse_and_instantiate_protocol(fileAccessMode_t access)
{
    create_devicespec(access);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        status.error = NDEV_STATUS::INVALID_DEVICESPEC;
        rs232_error();
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
        status.error = NDEV_STATUS::GENERAL;
        rs232_error();
        return;
    }
}

#ifdef ESP_PLATFORM
/**
 * Start the Interrupt rate limiting timer
 */
void rs232Network::timer_start()
{
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &rateTimerHandle);
    esp_timer_start_periodic(rateTimerHandle, timerRate * 1000);
}

/**
 * Stop the Interrupt rate limiting timer
 */
void rs232Network::timer_stop()
{
    // Delete existing timer
    if (rateTimerHandle != nullptr)
    {
        Debug_println("Deleting existing rateTimer\n");
        esp_timer_stop(rateTimerHandle);
        esp_timer_delete(rateTimerHandle);
        rateTimerHandle = nullptr;
    }
}
#endif /* ESP_PLATFORM */

/**
 * We were passed a COPY arg from DOS 2. This is complex, because we need to parse the comma,
 * and figure out one of three states:
 *
 * (1) we were passed D1:FOO.TXT,N:FOO.TXT, the second arg is ours.
 * (2) we were passed N:FOO.TXT,D1:FOO.TXT, the first arg is ours.
 * (3) we were passed N1:FOO.TXT,N2:FOO.TXT, get whichever one corresponds to our device ID.
 *
 * DeviceSpec will be transformed to only contain the relevant part of the deviceSpec, sans comma.
 */
void rs232Network::processCommaFromDevicespec(unsigned int dev)
{
    size_t comma_pos = deviceSpec.find(",");
    vector<string> tokens;

    if (comma_pos == string::npos)
        return; // no comma

    tokens = util_tokenize(deviceSpec, ',');

    for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        string item = *it;

        Debug_printf("processCommaFromDeviceSpec() found one.\n");

        if (item[0] != 'N')
            continue;                                       // not us.
        else if (item[1] == ':' && dev != FUJI_DEVICEID_NETWORK) // N: but we aren't N1:
            continue;                                       // also not us.
        else
        {
            // This is our deviceSpec.
            deviceSpec = item;
            break;
        }
    }

    Debug_printf("Passed back deviceSpec %s\n", deviceSpec.c_str());
}

/**
 * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
 */
void rs232Network::rs232_assert_interrupt()
{
#ifdef ESP_PlATFORM
    fnSystem.digital_write(PIN_RS232_RI, interruptProceed == true ? DIGI_HIGH : DIGI_LOW);
#endif /* ESP_PLATFORM */
}

void rs232Network::rs232_set_translation(netProtoTranslation_t mode)
{
    trans_mode = mode;
    rs232_complete();
}

void rs232Network::rs232_parse_json()
{
    json.parse();
    rs232_complete();
}

void rs232Network::rs232_set_json_query()
{
    uint8_t in[256];
    uint8_t *tmp;

    memset(in, 0, sizeof(in));

    bus_to_peripheral(in, sizeof(in));

    // strip away line endings from input spec.
    for (int i = 0; i < 256; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    Debug_printv("Q: %s\n",in);
    json.setReadQuery(string((char *) in),0);
    json_bytes_remaining = json.readValueLen();
    tmp = (uint8_t *)malloc(json.readValueLen());
    json.readValue(tmp,json_bytes_remaining);
    *receiveBuffer += string((const char *)tmp,json_bytes_remaining);
    free(tmp);
    Debug_printf("Query set to %s\n",in);
    rs232_complete();
}

void rs232Network::rs232_set_timer_rate(int newRate)
{
    timerRate = newRate;

#ifdef ESP_PLATFORM
    // Stop extant timer
    timer_stop();

    // Restart timer if we're running a protocol.
    if (protocol != nullptr)
        timer_start();
#endif /* ESP_PLATFORM */

    rs232_complete();
}

void rs232Network::process_fs(FujiBusPacket &packet)
{
    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol);
    if (!fs)
    {
        rs232_nak();
        return;
    }

    protocolError_t err;
    auto url = urlParser.get();
    switch (packet.command())
    {
    case NETCMD_RENAME:
        err = fs->rename(url);
        break;
    case NETCMD_DELETE:
        err = fs->del(url);
        break;
    case NETCMD_LOCK:
        err = fs->lock(url);
        break;
    case NETCMD_UNLOCK:
        err = fs->unlock(url);
        break;
    case NETCMD_MKDIR:
        err = fs->mkdir(url);
        break;
    case NETCMD_RMDIR:
        err = fs->rmdir(url);
        break;
    default:
        rs232_nak();
        return;
    }

    if (err != PROTOCOL_ERROR::NONE)
        rs232_error();
    else
        rs232_complete();
}

#endif /* BUILD_RS232 */
