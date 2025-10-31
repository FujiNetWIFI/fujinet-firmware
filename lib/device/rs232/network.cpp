#ifdef BUILD_RS232

/**
 * N: Firmware
 */

#include "network.h"
#include "fujiDevice.h"

#include <cstring>
#include <algorithm>
#include <endian.h>

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

    json.setLineEnding("\r\n"); // use ATASCII EOL for JSON records
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
void rs232Network::rs232_open(FujiTranslationMode mode, uint8_t spec)
{
    Debug_println("rs232Network::rs232_open()\n");

    rs232_ack();

    channelMode = PROTOCOL;

#ifdef ESP_PLATFORM
    // Delete timer if already extant.
    timer_stop();
#endif /* ESP_PLATFORM */

#ifdef OBSOLETE
    // persist aux1/aux2 values
    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;
    open_aux2 |= trans_aux2;
    cmdFrame.aux2 |= trans_aux2;
#endif /* OBSOLETE */

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
    parse_and_instantiate_protocol(spec);

    if (protocol == nullptr)
    {
        // invalid devicespec error already passed in.
        rs232_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), mode) == true)
    {
        status.error = protocol->error;
        Debug_printf("Protocol unable to make connection. Error: %d\n", status.error);
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
    json.setLineEnding("\r\n");
    protocol->setLineEnding("\r\n");
    channelMode = PROTOCOL;

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
    if (protocol->close())
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
    bool err = false;

    Debug_printf("rs232Network::rs232_read( %d bytes)\n", length);

    rs232_ack();

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        rs232_error();
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        status.error = NETWORK_ERROR_NOT_CONNECTED;
        rs232_error();
        return;
    }

    // Do the channel read
    err = rs232_read_channel(length);

    // And send off to the computer
    bus_to_computer((uint8_t *)receiveBuffer->data(), length, err);
    receiveBuffer->clear();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
bool rs232Network::rs232_read_channel_json(uint16_t num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining=0;
    else
        json_bytes_remaining-=num_bytes;

    return false;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool rs232Network::rs232_read_channel(uint16_t num_bytes)
{
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->read(num_bytes);
        break;
    case JSON:
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
    bool err = false;

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
        status.error = NETWORK_ERROR_NOT_CONNECTED;
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
    if (err == false)
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
bool rs232Network::rs232_write_channel(uint16_t num_bytes)
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
        default_status[3] = status.error;
        bus_to_computer(default_status, 4, false);
    }
}

bool rs232Network::rs232_status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? 1 : 136;
    //ns->rxBytesWaiting = json_bytes_remaining;
    return false; // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void rs232Network::rs232_status_channel()
{
    NDeviceStatus nstatus;
    bool err = false;

    Debug_printf("rs232Network::rs232_status_channel(%u)\n", channelMode);

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling rs232_status_channel on a null protocol.\r\n");
            err = true;
            status.error = true;
        } else {
            err = protocol->status(&status);
        }
        break;
    case JSON:
        rs232_status_channel_json(&status);
        break;
    }

    // Serialize status into status bytes
    size_t avail = protocol->available();
    avail = avail > 65535 ? 65535 : avail;
    nstatus.avail = htole16(avail);
    nstatus.conn = status.connected;
    nstatus.err = status.error;

    Debug_printf("rs232_status_channel() - BW: %u C: %u E: %u\n",
                 nstatus.avail, nstatus.conn, nstatus.err);

    // and send to computer
    bus_to_computer((uint8_t *) &nstatus, sizeof(nstatus), err);
}

/**
 * Get Prefix
 */
void rs232Network::rs232_get_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));
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

    memset(prefixSpec, 0, sizeof(prefixSpec));

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
void rs232Network::rs232_set_channel_mode(FujiChannelMode chanMode) // was aux2
{
    switch (chanMode)
    {
    case 0:
        channelMode = PROTOCOL;
        rs232_complete();
        break;
    case 1:
        channelMode = JSON;
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

    memset(loginSpec, 0, sizeof(loginSpec));
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

    memset(passwordSpec, 0, sizeof(passwordSpec));
    bus_to_peripheral(passwordSpec, sizeof(passwordSpec));
    util_devicespec_fix_9b(passwordSpec, sizeof(passwordSpec));

    password = string((char *)passwordSpec);
    rs232_complete();
}

#ifdef OBSOLETE
/**
 * RS232 Special, called as a default for any other RS232 command not processed by the other rs232_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either rs232_complete()
 * or rs232_error() is called.
 */
void rs232Network::rs232_special(unsigned int command)
{
    do_inquiry(command);

    switch (inq_dstats)
    {
    case DIRECTION_NONE:  // No payload
        rs232_ack();
        rs232_special_00();
        break;
    case DIRECTION_READ:  // Payload to Atari
        rs232_ack();
        rs232_special_40();
        break;
    case DIRECTION_WRITE: // Payload to Peripheral
        rs232_ack();
        rs232_special_80();
        break;
    default:
        rs232_nak();
        break;
    }
}
#endif /* OBSOLETE */

#ifdef OBSOLETE
/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: RS232 call.
 */
void rs232Network::rs232_special_inquiry()
{
    // Acknowledge
    rs232_ack();

    Debug_printf("rs232Network::rs232_special_inquiry(%02x)\n", cmdFrame.aux1);

    do_inquiry(cmdFrame.aux1);

    // Finally, return the completed inq_dstats value back to Atari
    bus_to_computer(&inq_dstats, sizeof(inq_dstats), false); // never errors.
}
#endif /* OBSOLETE */

void rs232Network::do_inquiry(unsigned char inq_cmd)
{
    // Reset inq_dstats
    inq_dstats = DIRECTION_INVALID;

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
        inq_dstats = protocol->special_inquiry(inq_cmd);

    // If we didn't get one from protocol, or unsupported, see if supported globally.
    if (inq_dstats == DIRECTION_INVALID)
    {
        switch (inq_cmd)
        {
        case FUJI_CMD_RENAME:
        case FUJI_CMD_DELETE:
        case FUJI_CMD_LOCK:
        case FUJI_CMD_UNLOCK:
        case FUJI_CMD_MKDIR:
        case FUJI_CMD_RMDIR:
        case FUJI_CMD_CHDIR:
        case FUJI_CMD_USERNAME:
        case FUJI_CMD_PASSWORD:
            inq_dstats = DIRECTION_WRITE;
            break;
        case FUJI_CMD_JSON:
            inq_dstats = DIRECTION_NONE;
            break;
        case FUJI_CMD_GETCWD:
            inq_dstats = DIRECTION_READ;
            break;
        case FUJI_CMD_TIMER: // Set interrupt rate
            inq_dstats = DIRECTION_NONE;
            break;
        case FUJI_CMD_TRANSLATION: // Set Translation
            inq_dstats = DIRECTION_NONE;
            break;
        case FUJI_CMD_PARSE: // JSON Parse
            if (channelMode == JSON)
                inq_dstats = DIRECTION_NONE;
            break;
        case FUJI_CMD_QUERY: // JSON Query
            if (channelMode == JSON)
                inq_dstats = DIRECTION_NONE;
            break;
        default:
            inq_dstats = DIRECTION_INVALID; // not supported
            break;
        }
    }

    Debug_printf("inq_dstats = %u\n", inq_dstats);
}

#ifdef OBSOLETE
/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal rs232_complete() or error().
 */
void rs232Network::rs232_special_00()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case FUJI_CMD_PARSE:
        if (channelMode == JSON)
            rs232_parse_json();
        break;
    case FUJI_CMD_TRANSLATION:
        rs232_set_translation();
        break;
    case FUJI_CMD_TIMER:
        rs232_set_timer_rate();
        break;
    case FUJI_CMD_JSON: // SET CHANNEL MODE
        rs232_set_channel_mode();
        break;
    default:
        if (protocol->special_00(&cmdFrame) == false)
            rs232_complete();
        else
            rs232_error();
    }
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void rs232Network::rs232_special_40()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case FUJI_CMD_GETCWD:
        rs232_get_prefix();
        return;
    }

    bus_to_computer((uint8_t *)receiveBuffer->data(),
                    SPECIAL_BUFFER_SIZE,
                    protocol->special_40((uint8_t *)receiveBuffer->data(), SPECIAL_BUFFER_SIZE, &cmdFrame));
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void rs232Network::rs232_special_80()
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case FUJI_CMD_RENAME:
    case FUJI_CMD_DELETE:
    case FUJI_CMD_LOCK:
    case FUJI_CMD_UNLOCK:
    case FUJI_CMD_MKDIR:
    case FUJI_CMD_RMDIR:
        rs232_do_idempotent_command_80();
        return;
    case FUJI_CMD_CHDIR:
        rs232_set_prefix();
        return;
    case FUJI_CMD_QUERY:
        if (channelMode == JSON)
            rs232_set_json_query();
        return;
    case FUJI_CMD_USERNAME:
        rs232_set_login();
        return;
    case FUJI_CMD_PASSWORD:
        rs232_set_password();
        return;
    }

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    bus_to_peripheral(spData, SPECIAL_BUFFER_SIZE);

    Debug_printf("rs232Network::rs232_special_80() - %s\n", spData);

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
        rs232_complete();
    else
        rs232_error();
}
#endif /* OBSOLETE */

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
        status.error = NETWORK_ERROR_SERVER_GENERAL;
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
void rs232Network::rs232_process(FujiBusCommand &command)
{
    switch (command.command)
    {
    case FUJI_CMD_HIGHSPEED:
        rs232_ack();
        rs232_high_speed();
        break;
    case FUJI_CMD_OPEN:
        rs232_open(static_cast<FujiTranslationMode>(command.fields[0]), command.fields[1]);
        break;
    case FUJI_CMD_CLOSE:
        rs232_close();
        break;
    case FUJI_CMD_READ:
        rs232_read(command.fields[0]);
        break;
    case FUJI_CMD_WRITE:
        rs232_write(command.fields[0]);
        break;
    case FUJI_CMD_STATUS:
        rs232_status(static_cast<FujiStatusReq>(command.fields[0]));
        break;
    case FUJI_CMD_PARSE:
        rs232_ack();
        rs232_parse_json();
        break;
    case FUJI_CMD_QUERY:
        rs232_ack();
        rs232_set_json_query(static_cast<FujiTranslationMode>(command.fields[1]));
        break;
    case FUJI_CMD_JSON:
        rs232_ack();
        rs232_set_channel_mode(static_cast<FujiChannelMode>(command.fields[0]));
        break;
    case FUJI_CMD_SPECIAL_QUERY:
        rs232_special_inquiry();
        break;
    case FUJI_CMD_SEEK:
        rs232_seek(command.fields[0]);
        break;
    case FUJI_CMD_TELL:
        rs232_tell();
        break;
#ifdef OBSOLETE
    default:
        rs232_special();
        break;
#endif /* OBSOLETE */
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
#if !defined(FUJINET_OVER_USB) && defined(ESP_PLATFORM)
        else
            fnSystem.digital_write(PIN_RS232_RI,DIGI_HIGH);
#endif /* FUJINET_OVER_USB */

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
void rs232Network::create_devicespec(uint8_t spec)
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

    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix, spec == 6, true);
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

void rs232Network::parse_and_instantiate_protocol(uint8_t spec)
{
    create_devicespec(spec);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        status.error = NETWORK_ERROR_INVALID_DEVICESPEC;
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
        status.error = NETWORK_ERROR_GENERAL;
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
        else if (item[1] == ':' && dev != 0x71) // N: but we aren't N1:
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
#if !defined(FUJINET_OVER_USB) && defined(ESP_PLATFORM)
    fnSystem.digital_write(PIN_RS232_RI, interruptProceed == true ? DIGI_HIGH : DIGI_LOW);
#endif /* FUJINET_OVER_USB */
}

void rs232Network::rs232_set_translation(FujiTranslationMode mode) // was aux2
{
    trans_mode = mode;
    rs232_complete();
}

void rs232Network::rs232_parse_json()
{
    json.parse();
    rs232_complete();
}

void rs232Network::rs232_set_json_query(FujiTranslationMode mode) // was aux2
{
    uint8_t in[256];
    const char *inp = NULL;
    uint8_t *tmp;

    memset(in, 0, sizeof(in));

    bus_to_peripheral(in, sizeof(in));

    // strip away line endings from input spec.
    for (int i = 0; i < 256; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    inp = strrchr((const char *)in, ':');
    inp++;
    Debug_printv("Q: %s\n",in);
    json.setReadQuery(string(inp), mode);
    json_bytes_remaining = json.readValueLen();
    tmp = (uint8_t *)malloc(json.readValueLen());
    json.readValue(tmp,json_bytes_remaining);
    *receiveBuffer += string((const char *)tmp,json_bytes_remaining);
    free(tmp);
    Debug_printf("Query set to %s\n",inp);
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

#ifdef OBSOLETE
void rs232Network::rs232_do_idempotent_command_80()
{
    rs232_ack();

    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        Debug_printf("Protocol = NULL\n");
        rs232_error();
        return;
    }

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame) == true)
    {
        Debug_printf("perform_idempotent_80 failed\n");
        rs232_error();
    }
    else
        rs232_complete();
}
#endif /* OBSOLETE */

#endif /* BUILD_RS232 */
