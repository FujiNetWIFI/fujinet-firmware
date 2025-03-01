#ifdef BUILD_ATARI

/**
 * N: Firmware
 */

#include "network.h"

#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <memory>

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

//
// TODO: add checksum handling when calling bus_to_peripheral() !
//


/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
 * flag to true. This is set to false when the interrupt is serviced.
 */
#ifdef ESP_PLATFORM
void onTimer(void *info)
{
    sioNetwork *parent = (sioNetwork *)info;
    portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptProceed = !parent->interruptProceed;
    portEXIT_CRITICAL_ISR(&parent->timerMux);
}
#endif

/**
 * Constructor
 */
sioNetwork::sioNetwork()
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
sioNetwork::~sioNetwork()
{
#ifndef ESP_PLATFORM // TODO apc: can be be in both?
    timer_stop();
#endif

    // first, delete protocol instance
    if (protocol != nullptr)
        delete protocol;
    protocol = nullptr;

    // then delete all buffers
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

/** SIO COMMANDS ***************************************************************/

/**
 * SIO Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void sioNetwork::sio_open()
{
    Debug_println("sioNetwork::sio_open()");

    sio_late_ack();

    auto prevCapacity = newData.capacity();
    newData.resize(NEWDATA_SIZE);
    auto newCapacity = newData.capacity();

    if (newCapacity < NEWDATA_SIZE || newData.size() != NEWDATA_SIZE) {
        Debug_printv("Could not allocate write buffer prev: %d, requested: %d\n", prevCapacity, NEWDATA_SIZE);
        sio_error();
        return;
    }

    channelMode = PROTOCOL;

    // Delete timer if already extant.
    timer_stop();

    // persist aux1/aux2 values - NOTHING USES THEM!
    open_aux1 = cmdFrame.aux1;

    // Ignore aux2 value if NTRANS set 0xFF, for ACTION!
    if (trans_aux2 == 0xFF)
    {
        open_aux2 = cmdFrame.aux2 = 0;
    }
    else if (cmdFrame.aux1 == 6) // don't xlate dir listings.
    {
        open_aux2 = cmdFrame.aux2;
    }
    else
    {
        open_aux2 = cmdFrame.aux2;
        open_aux2 |= trans_aux2;
        cmdFrame.aux2 |= trans_aux2;
    }

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

    if (json != nullptr) {
        delete json;
        json = nullptr;
    }

    if (urlParser != nullptr) {
        urlParser = nullptr;
    }

    // Reset status buffer
    status.reset();

    // Parse and instantiate protocol
    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        // invalid devicespec error already passed in.
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }

        // sio_error() - was already called from parse_and_instantiate_protocol()
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), &cmdFrame) == true)
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

        sio_error();
        return;
    }

    // Everything good, start the interrupt timer!
    timer_start();

    // Go ahead and send an interrupt, so Atari knows to get status.
    protocol->forceStatus = true;

    // TODO: Finally, go ahead and let the parsers know
    json = new FNJSON();
    json->setLineEnding("\x9b");
    json->setProtocol(protocol);
    channelMode = PROTOCOL;

    // And signal complete!
    sio_complete();
}

/**
 * SIO Close command
 * Tear down everything set up by sio_open(), as well as RX interrupt.
 */
void sioNetwork::sio_close()
{
    // Debug_printf("sioNetwork::sio_close()\n");

#ifdef ESP_PLATFORM
    long before_heap = esp_get_free_internal_heap_size();
#endif
    sio_ack();

    status.reset();

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        sio_complete();
        return;
    }

    // Ask the protocol to close
    if (protocol->close())
        sio_error();
    else
        sio_complete();

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    if (json != nullptr)
    {
        delete json;
        json = nullptr;
    }

#ifdef ESP_PLATFORM
    long after_heap = esp_get_free_internal_heap_size();
    Debug_printv("Before/After deleting: %lu/%lu (diff: %lu)", before_heap, after_heap, after_heap - before_heap);
#endif
}

/**
 * SIO Read command
 * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
 * fill the rest with nulls and return ERROR.
 *
 * @note It is the channel's responsibility to pad to required length.
 */
void sioNetwork::sio_read()
{
    unsigned short num_bytes = sio_get_aux();
    bool err = false;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_read(%d bytes)\n", num_bytes);
#endif

    sio_ack();

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        sio_error();
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }

        status.error = NETWORK_ERROR_NOT_CONNECTED;
        sio_error();
        return;
    }

    // Do the channel read
    err = sio_read_channel(num_bytes);

    // And send off to the computer
    bus_to_computer((uint8_t *)receiveBuffer->data(), num_bytes, err);
    receiveBuffer->erase(0, num_bytes);
    receiveBuffer->shrink_to_fit();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
bool sioNetwork::sio_read_channel_json(unsigned short num_bytes)
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
bool sioNetwork::sio_read_channel(unsigned short num_bytes)
{
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->read(num_bytes);
        break;
    case JSON:
        err = sio_read_channel_json(num_bytes);
        break;
    }
    return err;
}

/**
 * SIO Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to SIO. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void sioNetwork::sio_write()
{
    unsigned short num_bytes = sio_get_aux();
    bool err = false;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_write(%d bytes)\n", num_bytes);
#endif

    // sio_ack(); // apc: not yet

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        status.error = NETWORK_ERROR_NOT_CONNECTED;
        sio_error();
        return;
    }

    sio_late_ack();

    // Get the data from the Atari
    bus_to_peripheral(newData.data(), num_bytes); // TODO test checksum
    *transmitBuffer += string((char *)newData.data(), num_bytes);

    // Do the channel write
    err = sio_write_channel(num_bytes);

    // Acknowledge to Atari of channel outcome.
    if (err == false)
    {
        sio_complete();
    }
    else
    {
        sio_error();
    }
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit sio_error or sio_complete().
 */
bool sioNetwork::sio_write_channel(unsigned short num_bytes)
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
 * SIO Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to SIO.
 */
void sioNetwork::sio_status()
{
    // Acknowledge
    sio_ack();

    if (protocol == nullptr)
        sio_status_local();
    else
        sio_status_channel();
}

/**
 * @brief perform local status commands, if protocol is not bound, based on cmdFrame
 * value.
 */
void sioNetwork::sio_status_local()
{
    uint8_t ipAddress[4];
    uint8_t ipNetmask[4];
    uint8_t ipGateway[4];
    uint8_t ipDNS[4];
    uint8_t default_status[4] = {0, 0, 0, 0};

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_status_local(%u)\n", cmdFrame.aux2);
#endif

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (cmdFrame.aux2)
    {
    case 1: // IP Address
#ifdef VERBOSE_PROTOCOL
        Debug_printf("IP Address: %u.%u.%u.%u\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
#endif
        bus_to_computer(ipAddress, 4, false);
        break;
    case 2: // Netmask
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
#endif
        bus_to_computer(ipNetmask, 4, false);
        break;
    case 3: // Gatway
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
#endif
        bus_to_computer(ipGateway, 4, false);
        break;
    case 4: // DNS
#ifdef VERBOSE_PROTOCOL
        Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
#endif
        bus_to_computer(ipDNS, 4, false);
        break;
    default:
        default_status[2] = status.connected;
        default_status[3] = status.error;
        bus_to_computer(default_status, 4, false);
    }
}

bool sioNetwork::sio_status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? 1 : 136;
    ns->rxBytesWaiting = json_bytes_remaining;
    return false; // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void sioNetwork::sio_status_channel()
{
    uint8_t serialized_status[4] = {0, 0, 0, 0};
    bool err = false;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_status_channel(mode: %u)\n", channelMode);
#endif

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = true;
            status.error = true;
        } else {
            err = protocol->status(&status);
        }
        break;
    case JSON:
        sio_status_channel_json(&status);
        break;
    }
    // clear forced flag (first status after open)
    protocol->forceStatus = false;

    // Serialize status into status bytes
    serialized_status[0] = status.rxBytesWaiting & 0xFF;
    serialized_status[1] = status.rxBytesWaiting >> 8;
    serialized_status[2] = status.connected;
    serialized_status[3] = status.error;

    // leaving this one to print
    Debug_printf("sio_status_channel() - BW: %u C: %u E: %u\n", status.rxBytesWaiting, status.connected, status.error);

    // and send to computer
    bus_to_computer(serialized_status, sizeof(serialized_status), err);
}

/**
 * Get Prefix
 */
void sioNetwork::sio_get_prefix()
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
void sioNetwork::sio_set_prefix()
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
    sio_complete();
}

/**
 * @brief set channel mode
 */
void sioNetwork::sio_set_channel_mode()
{
    switch (cmdFrame.aux2)
    {
    case 0:
        channelMode = PROTOCOL;
        sio_complete();
        break;
    case 1:
        channelMode = JSON;
        sio_complete();
        break;
    default:
        sio_error();
    }
}

/**
 * Set login
 */
void sioNetwork::sio_set_login()
{
    uint8_t loginSpec[256];

    memset(loginSpec, 0, sizeof(loginSpec));
    bus_to_peripheral(loginSpec, sizeof(loginSpec)); // TODO test checksum
    util_devicespec_fix_9b(loginSpec, sizeof(loginSpec));

    login = string((char *)loginSpec);
    sio_complete();
}

/**
 * Set password
 */
void sioNetwork::sio_set_password()
{
    uint8_t passwordSpec[256];

    memset(passwordSpec, 0, sizeof(passwordSpec));
    bus_to_peripheral(passwordSpec, sizeof(passwordSpec)); // TODO test checksum
    util_devicespec_fix_9b(passwordSpec, sizeof(passwordSpec));

    password = string((char *)passwordSpec);
    sio_complete();
}

/**
 * SIO Special, called as a default for any other SIO command not processed by the other sio_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either sio_complete()
 * or sio_error() is called.
 */
void sioNetwork::sio_special()
{
    do_inquiry(cmdFrame.comnd);

    switch (inq_dstats)
    {
    case 0x00: // No payload
        sio_ack();
        sio_special_00();
        break;
    case 0x40: // Payload to Atari
        sio_ack();
        sio_special_40();
        break;
    case 0x80: // Payload to Peripheral
        sio_late_ack();
        sio_special_80();
        break;
    default:
        sio_nak();
        break;
    }
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: SIO call.
 */
void sioNetwork::sio_special_inquiry()
{
    // Acknowledge
    sio_ack();

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_special_inquiry(%02x)\n", cmdFrame.aux1);
#endif

    do_inquiry(cmdFrame.aux1);

    // Finally, return the completed inq_dstats value back to Atari
    bus_to_computer(&inq_dstats, sizeof(inq_dstats), false); // never errors.
}

void sioNetwork::do_inquiry(unsigned char inq_cmd)
{
    // Reset inq_dstats
    inq_dstats = 0xff;

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
    {
        inq_dstats = protocol->special_inquiry(inq_cmd);
#ifdef VERBOSE_PROTOCOL
        Debug_printf("protocol special_inquiry returned %d\r\n", inq_dstats);
#endif
    }

    // If we didn't get one from protocol, or unsupported, see if supported globally.
    if (inq_dstats == 0xFF)
    {
        switch (inq_cmd)
        {
        case 0x20: // ' ' rename
        case 0x21: // '!' delete
        case 0x23: // '#' lock
        case 0x24: // '$' unlock
        case 0x2A: // '*' mkdir
        case 0x2B: // '+' rmdir
        case 0x2C: // ',' chdir/get prefix
        case 0xFD: //     login
        case 0xFE: //     password
            inq_dstats = 0x80;
            break;
        case 0xFC: //     channel mode
            inq_dstats = 0x00;
            break;
        case 0xFB: // String Processing mode, only in JSON mode
            if (channelMode == JSON)
                inq_dstats = 0x00;
            break;
        case 0x30: // '0' set prefix
            inq_dstats = 0x40;
            break;
        case 'Z': // Set interrupt rate
            inq_dstats = 0x00;
            break;
        case 'T': // Set Translation
            inq_dstats = 0x00;
            break;
        case 'P': // JSON Parse
            if (channelMode == JSON)
                inq_dstats = 0x00;
            break;
        case 'Q': // JSON Query
            if (channelMode == JSON)
                inq_dstats = 0x80;
            break;
        default:
            inq_dstats = 0xFF; // not supported
            break;
        }
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("inq_dstats = %u\n", inq_dstats);
#endif
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal sio_complete() or error().
 */
void sioNetwork::sio_special_00()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case 'P':
        if (channelMode == JSON)
            sio_parse_json();
        break;
    case 'T':
        sio_set_translation();
        break;
    case 'Z':
        sio_set_timer_rate();
        break;
    case 0xFB: // JSON parameter wrangling
        sio_set_json_parameters();
        break;
    case 0xFC: // SET CHANNEL MODE
        sio_set_channel_mode();
        break;
    default:
        if (protocol->special_00(&cmdFrame) == false)
            sio_complete();
        else
            sio_error();
    }
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void sioNetwork::sio_special_40()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case 0x30:
        sio_get_prefix();
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
void sioNetwork::sio_special_80()
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case 0x20: // RENAME  ' '
    case 0x21: // DELETE  '!'
    case 0x23: // LOCK    '#'
    case 0x24: // UNLOCK  '$'
    case 0x2A: // MKDIR   '*'
    case 0x2B: // RMDIR   '+'
        sio_do_idempotent_command_80();
        return;
    case 0x2C: // CHDIR   ','
        sio_set_prefix();
        return;
    case 'Q':
        if (channelMode == JSON)
            sio_set_json_query();
        return;
    case 0xFD: // LOGIN
        sio_set_login();
        return;
    case 0xFE: // PASSWORD
        sio_set_password();
        return;
    }

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    bus_to_peripheral(spData, SPECIAL_BUFFER_SIZE); // TODO test checksum

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_special_80() - %s\n", spData);
#endif

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
        sio_complete();
    else
        sio_error();
}

/**
 * Process incoming SIO command for device 0x7X
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void sioNetwork::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    // leaving this one to print
    Debug_printf("sioNetwork::sio_process 0x%02hx '%c': 0x%02hx, 0x%02hx\n", cmdFrame.comnd, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);

    switch (cmdFrame.comnd)
    {
    case 0x3F:
        sio_ack();
        sio_high_speed();
        break;
    case 'O':
        sio_open();
        break;
    case 'C':
        sio_close();
        break;
    case 'R':
        sio_read();
        break;
    case 'W':
        sio_write();
        break;
    case 'S':
        sio_status();
        break;
    case 0xFF:
        sio_special_inquiry();
        break;
    default:
        sio_special();
        break;
    }
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed (continue toggling PROCEED).
 */
void sioNetwork::sio_poll_interrupt()
{
    if (protocol != nullptr)
    {
        if (protocol->interruptEnable == false)
            return;

        /* assert interrupt if we need Status call from host to arrive */
        if (protocol->forceStatus == true)
        {
            sio_assert_interrupt();
            return;
        }

        protocol->fromInterrupt = true;
        protocol->status(&status);
        protocol->fromInterrupt = false;

        if (status.rxBytesWaiting > 0 || status.connected == 0)
            sio_assert_interrupt();
#ifndef ESP_PLATFORM
        else
            sio_clear_interrupt();
#endif

        reservedSave = status.connected;
        errorSave = status.error;
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool sioNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }
    
    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("sioNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    // leaving this one to print
    Debug_printf("sioNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void sioNetwork::create_devicespec()
{
    // Clean up devicespec buffer.
    memset(devicespecBuf, 0, sizeof(devicespecBuf));

    // Get Devicespec from buffer, and put into primary devicespec string
    bus_to_peripheral(devicespecBuf, sizeof(devicespecBuf)); // TODO test checksum
    util_devicespec_fix_9b(devicespecBuf, sizeof(devicespecBuf));
    deviceSpec = string((char *)devicespecBuf);
    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix, cmdFrame.aux1 == 6, true);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void sioNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void sioNetwork::parse_and_instantiate_protocol()
{
    create_devicespec();
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        status.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        sio_error();
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
        sio_error();
        return;
    }
}

/**
 * Start the Interrupt rate limiting timer
 */
void sioNetwork::timer_start()
{
#ifdef ESP_PLATFORM
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &rateTimerHandle);
    esp_timer_start_periodic(rateTimerHandle, timerRate * 1000);
#else
    lastInterruptMs = fnSystem.millis() - timerRate;
#endif
}

/**
 * Stop the Interrupt rate limiting timer
 */
void sioNetwork::timer_stop()
{
#ifdef ESP_PLATFORM
    // Delete existing timer
    if (rateTimerHandle != nullptr)
    {
        Debug_println("Deleting existing rateTimer\n");
        esp_timer_stop(rateTimerHandle);
        esp_timer_delete(rateTimerHandle);
        rateTimerHandle = nullptr;
    }
#endif
}

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
void sioNetwork::processCommaFromDevicespec()
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
        else if (item[1] == ':' && cmdFrame.device != 0x71) // N: but we aren't N1:
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
void sioNetwork::sio_assert_interrupt()
{
#ifdef ESP_PLATFORM
    fnSystem.digital_write(PIN_PROC, interruptProceed == true ? DIGI_HIGH : DIGI_LOW);
    // Debug_print(interruptProceed ? "+" : "-");
#else
    uint64_t ms = fnSystem.millis();
    if (ms - lastInterruptMs >= timerRate)
    {
        interruptProceed = !interruptProceed;
        fnSioCom.set_proceed(interruptProceed);
        lastInterruptMs = ms;
    }
#endif
}

#ifndef ESP_PLATFORM
/**
 * Called to clear the PROCEED interrupt
 */
void sioNetwork::sio_clear_interrupt()
{
    if (interruptProceed) 
    {
        // Debug_println("clear interrupt");
        interruptProceed = false;
        fnSioCom.set_proceed(interruptProceed);
        lastInterruptMs = fnSystem.millis();
    }
}
#endif

void sioNetwork::sio_set_translation()
{
    trans_aux2 = cmdFrame.aux2;
    sio_complete();
}

void sioNetwork::sio_parse_json()
{
    json->parse();
    sio_complete();
}

void sioNetwork::sio_set_json_query()
{
    uint8_t in[256];

    memset(in, 0, sizeof(in));

    bus_to_peripheral(in, sizeof(in)); // TODO test checksum

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

    json->setReadQuery(inp_string, cmdFrame.aux2);
    json_bytes_remaining = json->json_bytes_remaining;

    std::vector<uint8_t> tmp(json_bytes_remaining);
    json->readValue(tmp.data(), json_bytes_remaining);

    // don't copy past first nul char in tmp
    auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    *receiveBuffer += std::string(tmp.begin(), null_pos);

    Debug_printf("Query set to >%s<\r\n", inp_string.c_str());
    sio_complete();
}

void sioNetwork::sio_set_json_parameters()
{
    // aux1  | aux2    |    meaning
    // 0     | 0/1/2   |  Set the json->_queryParam value, which is the translation value for string processing
    // 1     |   c     |  Set the json->lineEnding = c, convert from char to single byte string

    switch (cmdFrame.aux1)
    {
    case 0:     // JSON QUERY PARAM
        if (cmdFrame.aux2 > 2)
        {
            sio_error();
            return;
        }
        json->setQueryParam(cmdFrame.aux2);
        sio_complete();
        break;
    case 1:     // LINE ENDING
    {
        std::stringstream ss;
        ss << cmdFrame.aux2;
        string new_le = ss.str();
        Debug_printf("JSON line ending changed to 0x%02hx\r\n", cmdFrame.aux2);
        json->setLineEnding(new_le);
        sio_complete();
        break;
    }
    default:
        sio_error();
        break;
    }
}

void sioNetwork::sio_set_timer_rate()
{
    timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // Stop extant timer
    timer_stop();

    // Restart timer if we're running a protocol.
    if (protocol != nullptr)
        timer_start();

    sio_complete();
}

void sioNetwork::sio_do_idempotent_command_80()
{
    Debug_printf("sioNetwork::sio_do_idempotent_command_80()\r\n");
    // sio_ack() - was already called from sio_special()

    // Shut down protocol left from previous command, if any
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
    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        Debug_printf("Protocol = NULL\n");
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        // sio_error() - was already called from parse_and_instantiate_protocol()
        return;
    }

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame) == true)
    {
        Debug_printf("perform_idempotent_80 failed\n");
        sio_error();
    }
    else
        sio_complete();
}

#endif /* BUILD_ATARI */
