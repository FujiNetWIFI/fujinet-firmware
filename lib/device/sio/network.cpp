#ifdef BUILD_ATARI

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"

#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <memory>
#include <sstream>

#include "../../include/debug.h"
#include "../../include/pinmap.h"

#include "fnSystem.h"
#include "utils.h"
#include "fuji_endian.h"

#include "status_error_codes.h"

#include "TCP.h"
#include "UDP.h"
#include "HTTP.h"
#include "FS.h"

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

    transaction_begin(TRANS_STATE::WILL_GET);

    auto prevCapacity = newData.capacity();
    newData.resize(NEWDATA_SIZE);
    auto newCapacity = newData.capacity();

    if (newCapacity < NEWDATA_SIZE || newData.size() != NEWDATA_SIZE) {
        Debug_printv("Could not allocate write buffer prev: %d, requested: %d\n", prevCapacity, NEWDATA_SIZE);
        transaction_error();
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

        // transaction_error() - was already called from parse_and_instantiate_protocol()
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), (fileAccessMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2) != FUJI_ERROR::NONE)
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

        transaction_error();
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
    transaction_complete();
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
    transaction_begin(TRANS_STATE::NO_GET);

    status.reset();

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
    if (protocol->close() != FUJI_ERROR::NONE)
        transaction_error();
    else
        transaction_complete();

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
    uint16_t num_bytes = cmdFrame.aux12;
    fujiError_t err = FUJI_ERROR::NONE;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_read(%d bytes)\n", num_bytes);
#endif

    transaction_begin(TRANS_STATE::NO_GET);

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        status.error = NDEV_STATUS::COULD_NOT_ALLOCATE_BUFFERS;
        transaction_error();
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

        status.error = NDEV_STATUS::NOT_CONNECTED;
        transaction_error();
        return;
    }

    // Do the channel read
    err = sio_read_channel(num_bytes);

    // And send off to the computer
    transaction_put((uint8_t *)receiveBuffer->data(), num_bytes, err != FUJI_ERROR::NONE);
    receiveBuffer->erase(0, num_bytes);
    receiveBuffer->shrink_to_fit();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
fujiError_t sioNetwork::sio_read_channel_json(unsigned short num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining=0;
    else
        json_bytes_remaining-=num_bytes;

    return FUJI_ERROR::NONE;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Passed directly to transaction_put().
 */
fujiError_t sioNetwork::sio_read_channel(unsigned short num_bytes)
{
    fujiError_t err = FUJI_ERROR::NONE;

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
    uint16_t num_bytes = cmdFrame.aux12;
    fujiError_t err = FUJI_ERROR::NONE;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_write(%d bytes)\n", num_bytes);
#endif

    // transaction_begin(TRANS_STATE::NO_GET); // apc: not yet

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        status.error = NDEV_STATUS::NOT_CONNECTED;
        transaction_error();
        return;
    }

    transaction_begin(TRANS_STATE::WILL_GET);

    // Get the data from the Atari
    transaction_get(newData.data(), num_bytes); // TODO test checksum
    *transmitBuffer += string((char *)newData.data(), num_bytes);

    // Do the channel write
    err = sio_write_channel(num_bytes);

    // Acknowledge to Atari of channel outcome.
    if (err == FUJI_ERROR::NONE)
    {
        transaction_complete();
    }
    else
    {
        transaction_error();
    }
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return FUJI_ERROR::UNSPECIFIED on error, FUJI_ERROR::NONE on success. Used to emit transaction_error or transaction_complete().
 */
fujiError_t sioNetwork::sio_write_channel(unsigned short num_bytes)
{
    fujiError_t err = FUJI_ERROR::NONE;

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
    transaction_begin(TRANS_STATE::NO_GET);

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
    NDeviceStatus default_status {};

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
        transaction_put(ipAddress, 4, false);
        break;
    case 2: // Netmask
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
#endif
        transaction_put(ipNetmask, 4, false);
        break;
    case 3: // Gatway
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
#endif
        transaction_put(ipGateway, 4, false);
        break;
    case 4: // DNS
#ifdef VERBOSE_PROTOCOL
        Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
#endif
        transaction_put(ipDNS, 4, false);
        break;
    default:
        default_status.conn = status.connected;
        default_status.err = status.error;
        transaction_put((uint8_t *) &default_status, sizeof(default_status), false);
    }
}

error_is_true sioNetwork::sio_status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? NDEV_STATUS::SUCCESS : NDEV_STATUS::END_OF_FILE;
    RETURN_SUCCESS_AS_FALSE(); // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void sioNetwork::sio_status_channel()
{
    NDeviceStatus nstatus;
    size_t avail = 0;
    fujiError_t err = FUJI_ERROR::NONE;

#if 1 //def VERBOSE_PROTOCOL
    Debug_printf("sioNetwork::sio_status_channel(mode: %u)\n", channelMode);
#endif

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = FUJI_ERROR::UNSPECIFIED;
            status.error = NDEV_STATUS::NOT_CONNECTED;
        } else {
            err = protocol->status(&status);
            // Check receiveBuffer first — protocol->status() may have
            // auto-read data into it, but protocol->available() only
            // checks the underlying socket (which is now empty).
            avail = receiveBuffer->length();
            if (avail == 0)
                avail = protocol->available();
        }
        break;
    case JSON:
        sio_status_channel_json(&status);
        avail = json_bytes_remaining;
        break;
    }
    // clear forced flag (first status after open)
    protocol->forceStatus = false;

    // Serialize status into status bytes
    avail = avail > 65535 ? 65535 : avail;
    nstatus.avail = htole16(avail);
    nstatus.conn = status.connected;
    nstatus.err = status.error;

    // leaving this one to print
    Debug_printf("sio_status_channel() - BW: %u C: %u E: %u\n",
                 nstatus.avail, nstatus.conn, nstatus.err);

    // and send to computer
    transaction_put((uint8_t *) &nstatus, sizeof(nstatus), err != FUJI_ERROR::NONE);
}

/**
 * Get Prefix
 */
void sioNetwork::sio_get_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    transaction_begin(TRANS_STATE::NO_GET);

    memset(prefixSpec, 0, sizeof(prefixSpec));
    memcpy(prefixSpec, prefix.data(), prefix.size());

    prefixSpec[prefix.size()] = 0x9B; // add EOL.

    transaction_put(prefixSpec, sizeof(prefixSpec), false);
}

/**
 * Set Prefix
 */
void sioNetwork::sio_set_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    transaction_begin(TRANS_STATE::NO_GET);

    memset(prefixSpec, 0, sizeof(prefixSpec));

    transaction_get(prefixSpec, sizeof(prefixSpec)); // TODO test checksum
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
    transaction_complete();
}

/**
 * @brief set channel mode
 */
void sioNetwork::sio_set_channel_mode()
{
    transaction_begin(TRANS_STATE::NO_GET);

    switch (cmdFrame.aux2)
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
    }
}

/**
 * Set login
 */
void sioNetwork::sio_set_login()
{
    uint8_t loginSpec[256];

    transaction_begin(TRANS_STATE::WILL_GET);
    memset(loginSpec, 0, sizeof(loginSpec));
    transaction_get(loginSpec, sizeof(loginSpec)); // TODO test checksum
    util_devicespec_fix_9b(loginSpec, sizeof(loginSpec));

    login = string((char *)loginSpec);
    transaction_complete();
}

/**
 * Set password
 */
void sioNetwork::sio_set_password()
{
    uint8_t passwordSpec[256];

    transaction_begin(TRANS_STATE::NO_GET);
    memset(passwordSpec, 0, sizeof(passwordSpec));
    transaction_get(passwordSpec, sizeof(passwordSpec)); // TODO test checksum
    util_devicespec_fix_9b(passwordSpec, sizeof(passwordSpec));

    password = string((char *)passwordSpec);
    transaction_complete();
}

/**
 * Get DSTATS value for a given command.
 * This command allows CIO programs to query the data direction (DSTATS) for any network command.
 * The command code to query is passed in DAUX1 (aux1).
 * Returns a single byte: 0x00 (no payload), 0x40 (FujiNet→Atari), 0x80 (Atari→FujiNet), or 0xFF (invalid command).
 */
void sioNetwork::sio_get_dstats_value()
{
    transaction_begin(TRANS_STATE::NO_GET);
    uint8_t command = cmdFrame.aux1;
    uint8_t dstats = get_dstats_for_command(command);
    transaction_put(&dstats, 1, false);
}

/**
 * Seek (POINT) command
 * Set the file position from a 3-byte LE payload.
 */
void sioNetwork::sio_seek()
{
    uint8_t pos[3];
    off_t offset;

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        status.error = NDEV_STATUS::NOT_CONNECTED;
        transaction_error();
        return;
    }

    if (channelMode != PROTOCOL)
    {
        status.error = NDEV_STATUS::INVALID_POINT;
        transaction_error();
        return;
    }

    transaction_begin(TRANS_STATE::WILL_GET);
    transaction_get(pos, sizeof(pos));

    offset = pos[0] | (pos[1] << 8) | (pos[2] << 16);

    if (protocol->seek(offset, SEEK_SET) == -1)
    {
        status.error = NDEV_STATUS::INVALID_POINT;
        transaction_error();
        return;
    }

    transaction_complete();
}

/**
 * Tell (NOTE) command
 * Return the current file position as a 3-byte LE payload.
 */
void sioNetwork::sio_tell()
{
    uint8_t pos[3] = {0, 0, 0};
    off_t offset = -1;

    transaction_begin(TRANS_STATE::NO_GET);

    if (protocol != nullptr && channelMode == PROTOCOL)
        offset = protocol->seek(0, SEEK_CUR);

    if (offset == -1)
    {
        status.error = protocol == nullptr ? NDEV_STATUS::NOT_CONNECTED : NDEV_STATUS::INVALID_POINT;
        transaction_put(pos, sizeof(pos), true);
        return;
    }

    pos[0] = offset & 0xFF;
    pos[1] = (offset >> 8) & 0xFF;
    pos[2] = (offset >> 16) & 0xFF;
    transaction_put(pos, sizeof(pos), false);
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
    Debug_printf("sioNetwork::sio_process 0x%02hx '%c': 0x%02hx, 0x%02hx baud: %d\n",
                 cmdFrame.comnd, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2,
                 SYSTEM_BUS.getBaudrate());

    switch (cmdFrame.comnd)
    {
    case NETCMD_HSIO_INDEX:
        sio_high_speed();
        break;
    case NETCMD_OPEN:
        sio_open();
        break;
    case NETCMD_CLOSE:
        sio_close();
        break;
    case NETCMD_READ:
        sio_read();
        break;
    case NETCMD_WRITE:
        sio_write();
        break;
    case NETCMD_STATUS:
        sio_status();
        break;

    case NETCMD_PARSE:
        sio_parse_json();
        break;
    case NETCMD_TRANSLATION:
        sio_set_translation();
        break;
    case NETCMD_SET_EOL:
        sio_set_eol();
        break;
    case NETCMD_SET_INT_RATE:
        sio_set_timer_rate();
        break;
    case NETCMD_SET_PARAMETERS: // JSON parameter wrangling
        sio_set_json_parameters();
        break;
    case NETCMD_CHANNEL_MODE:
        sio_set_channel_mode();
        break;

    case NETCMD_GETCWD:
        sio_get_prefix();
        break;

    case NETCMD_CHDIR:
        sio_set_prefix();
        return;
    case NETCMD_QUERY:
        sio_set_json_query();
        return;
    case NETCMD_USERNAME:
        sio_set_login();
        return;
    case NETCMD_PASSWORD:
        sio_set_password();
        return;

    case NETCMD_GET_DSTATS_VALUE:
        sio_get_dstats_value();
        break;

    case NETCMD_SEEK: // POINT
        sio_seek();
        break;
    case NETCMD_TELL: // NOTE
        sio_tell();
        break;

    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        process_fs();
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp();
        break;

    case NETCMD_SET_CHANNEL_MODE:
        process_http();
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp();
        break;

    default:
        transaction_error();
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

        if (protocol->available() > 0 || status.connected == 0)
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
 * Get the DSTATS value for a given network command.
 * DSTATS indicates the direction of data for a command:
 * - 0x00: No payload
 * - 0x40: Payload from FujiNet to Atari
 * - 0x80: Payload from Atari to FujiNet
 * - 0xFF: Invalid/unknown command
 *
 * @param command The network command code (typically from aux1)
 * @return The DSTATS byte value for that command
 */
uint8_t sioNetwork::get_dstats_for_command(uint8_t command)
{
    switch (command)
    {
    // No payload commands (0x00)
    case NETCMD_CLOSE:
    case NETCMD_PARSE:
    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
    case NETCMD_CHANNEL_MODE:
    case NETCMD_TRANSLATION:
    case NETCMD_SET_INT_RATE:
    case NETCMD_SET_PARAMETERS:
        return SIO_DIRECTION_NONE;

    // Payload from FujiNet to Atari (0x40)
    case NETCMD_HSIO_INDEX:
    case NETCMD_READ:
    case NETCMD_STATUS:
    case NETCMD_GETCWD:
    case NETCMD_TELL:
        return SIO_DIRECTION_READ;

    // Payload from Atari to FujiNet (0x80)
    case NETCMD_OPEN:
    case NETCMD_WRITE:
    case NETCMD_CHDIR:
    case NETCMD_QUERY:
    case NETCMD_USERNAME:
    case NETCMD_PASSWORD:
    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
    case NETCMD_SET_DESTINATION:
    case NETCMD_SEEK:
        return SIO_DIRECTION_WRITE;

    // Invalid/unknown command
    default:
        return SIO_DIRECTION_INVALID;
    }
}

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
success_is_true sioNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("sioNetwork::instantiate_protocol() - Could not create protocol.\n");
        RETURN_ERROR_AS_FALSE();
    }

    // Atari's native EOL is the ATASCII end-of-line (0x9B), unless the client
    // has overridden it with the NETCMD_SET_EOL command.
    protocol->native_eol = native_eol_override.empty() ? STR_ATASCII_EOL : native_eol_override;

    // leaving this one to print
    Debug_printf("sioNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    RETURN_SUCCESS_AS_TRUE();
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
    transaction_get(devicespecBuf, sizeof(devicespecBuf)); // TODO test checksum
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
        status.error = NDEV_STATUS::INVALID_DEVICESPEC;
        transaction_error();
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
        transaction_error();
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
        SYSTEM_BUS.set_proceed(interruptProceed);
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
        SYSTEM_BUS.set_proceed(interruptProceed);
        lastInterruptMs = fnSystem.millis();
    }
}
#endif

void sioNetwork::sio_set_translation()
{
    transaction_begin(TRANS_STATE::NO_GET);
    trans_aux2 = cmdFrame.aux2;
    transaction_complete();
}

void sioNetwork::sio_set_eol()
{
    transaction_begin(TRANS_STATE::NO_GET);

    // aux1/aux2 carry the EOL bytes; aux1==0 clears the override (restore default).
    native_eol_override.clear();
    if (cmdFrame.aux1 != 0x00)
    {
        native_eol_override.push_back((char)cmdFrame.aux1);
        if (cmdFrame.aux2 != 0x00)
            native_eol_override.push_back((char)cmdFrame.aux2);
    }

    // Apply to a live protocol immediately; restore default when cleared.
    if (protocol != nullptr)
        protocol->native_eol = native_eol_override.empty() ? STR_ATASCII_EOL : native_eol_override;

    transaction_complete();
}

void sioNetwork::sio_parse_json()
{
    transaction_begin(TRANS_STATE::NO_GET);
    json->parse();
    transaction_complete();
}

void sioNetwork::sio_set_json_query()
{
    uint8_t in[256];

    transaction_begin(TRANS_STATE::WILL_GET);

    memset(in, 0, sizeof(in));

    transaction_get(in, sizeof(in)); // TODO test checksum

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
    int query_bytes = json->available();
    json_bytes_remaining += query_bytes;

    std::vector<uint8_t> tmp(query_bytes);
    json->readValue(tmp.data(), query_bytes);

    // don't copy past first nul char in tmp
    auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    *receiveBuffer += std::string(tmp.begin(), null_pos);

    Debug_printf("Query set to >%s< (buf_size=%d, json_remaining=%d)\r\n",
                 inp_string.c_str(), (int)receiveBuffer->size(), json_bytes_remaining);
    transaction_complete();
}

void sioNetwork::sio_set_json_parameters()
{
    transaction_begin(TRANS_STATE::NO_GET);

    // aux1  | aux2    |    meaning
    // 0     | 0/1/2   |  Set the json->_queryParam value, which is the translation value for string processing
    // 1     |   c     |  Set the json->lineEnding = c, convert from char to single byte string

    switch (cmdFrame.aux1)
    {
    case 0:     // JSON QUERY PARAM
        if (cmdFrame.aux2 > 2)
        {
            transaction_error();
            return;
        }
        json->setQueryParam(cmdFrame.aux2);
        transaction_complete();
        break;
    case 1:     // LINE ENDING
    {
        std::stringstream ss;
        ss << cmdFrame.aux2;
        string new_le = ss.str();
        Debug_printf("JSON line ending changed to 0x%02hx\r\n", cmdFrame.aux2);
        json->setLineEnding(new_le);
        transaction_complete();
        break;
    }
    default:
        transaction_error();
        break;
    }
}

void sioNetwork::sio_set_timer_rate()
{
    transaction_begin(TRANS_STATE::NO_GET);
    timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // Stop extant timer
    timer_stop();

    // Restart timer if we're running a protocol.
    if (protocol != nullptr)
        timer_start();

    transaction_complete();
}

void sioNetwork::process_fs()
{
    transaction_begin(TRANS_STATE::WILL_GET); // command frame ACK must precede transaction_get() in parse_and_instantiate_protocol()

    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        // transaction_error() was already called from parse_and_instantiate_protocol()
        return;
    }

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(protocol);
    if (!fs)
    {
        transaction_error(); // ACK already sent; host expects C or E, not N
        delete protocol;
        protocol = nullptr;
        return;
    }

    fujiError_t err;
    auto url = urlParser.get();
    switch (cmdFrame.comnd)
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
        transaction_error(); // ACK already sent; host expects C or E, not N
        delete protocol;
        protocol = nullptr;
        return;
    }

    // Clean up the one-shot protocol created for this fs operation
    delete protocol;
    protocol = nullptr;
    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    if (err != FUJI_ERROR::NONE)
    {
        transaction_error();
        return;
    }

    transaction_complete();
}

void sioNetwork::process_tcp()
{
    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(protocol);
    if (!tcp)
    {
        transaction_error();
        return;
    }

    fujiError_t err;
    switch (cmdFrame.comnd)
    {
    case NETCMD_CONTROL:
        transaction_begin(TRANS_STATE::NO_GET);
        err = tcp->accept_connection();
        break;
    case NETCMD_CLOSE_CLIENT:
        transaction_begin(TRANS_STATE::NO_GET);
        err = tcp->close_client_connection();
        break;
    default:
        transaction_error();
        return;
    }

    if (err != FUJI_ERROR::NONE)
    {
        transaction_error();
        return;
    }

    transaction_complete();
}

void sioNetwork::process_http()
{
    // Make sure this is really an HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(protocol);
    if (!http)
    {
        transaction_error();
        return;
    }

    fujiError_t err;
    switch (cmdFrame.comnd)
    {
    case NETCMD_SET_CHANNEL_MODE:
        transaction_begin(TRANS_STATE::NO_GET);
        err = http->set_channel_mode((netProtoHTTPChannelMode_t) cmdFrame.aux2);
        break;
    default:
        transaction_error();
        return;
    }

    if (err != FUJI_ERROR::NONE)
    {
        transaction_error();
        return;
    }

    transaction_complete();
}

void sioNetwork::process_udp()
{
    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(protocol);
    if (!udp)
    {
        transaction_error();
        return;
    }

    fujiError_t err;
    switch (cmdFrame.comnd)
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        transaction_begin(TRANS_STATE::NO_GET);
        err = udp->get_remote(receiveBuffer->data(), SPECIAL_BUFFER_SIZE);
        transaction_put((uint8_t *)receiveBuffer->data(), SPECIAL_BUFFER_SIZE, err != FUJI_ERROR::NONE);
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            uint8_t spData[SPECIAL_BUFFER_SIZE];
            transaction_get(spData, sizeof(spData));
            err = udp->set_destination(spData, sizeof(spData));
            if (err != FUJI_ERROR::NONE)
                transaction_error();
            else
                transaction_complete();
        }
        break;
    default:
        transaction_error();
        return;
    }
}

#endif /* BUILD_ATARI */
