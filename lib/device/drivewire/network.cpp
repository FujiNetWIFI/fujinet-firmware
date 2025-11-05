#ifdef BUILD_COCO

/**
 * Network Firmware
 */

#include "network.h"

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
#include "fuji_endian.h"

using namespace std;

/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptCD
 * flag to true. This is set to false when the interrupt is serviced.
 */
#ifdef ESP_PLATFORM
void onTimer(void *info)
{
    drivewireNetwork *parent = (drivewireNetwork *)info;
    portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptCD = !parent->interruptCD;
    portEXIT_CRITICAL_ISR(&parent->timerMux);
}
#endif

/**
 * Constructor
 */
drivewireNetwork::drivewireNetwork()
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
drivewireNetwork::~drivewireNetwork()
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

/**
 * Start the Interrupt rate limiting timer
 */
void drivewireNetwork::timer_start()
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
void drivewireNetwork::timer_stop()
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

/** DRIVEWIRE COMMANDS ***************************************************************/

void drivewireNetwork::ready()
{
    SYSTEM_BUS.write(0x01); // yes, ready.
}

/**
 * DRIVEWIRE Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void drivewireNetwork::open()
{
    Debug_printf("drivewireNetwork::sio_open(%02x,%02x)\n",cmdFrame.aux1,cmdFrame.aux2);

    char tmp[256];

    size_t bytes_read = SYSTEM_BUS.read((uint8_t *)tmp, 256);
    tmp[sizeof(tmp)-1] = '\0';

    Debug_printf("tmp = %s\n",tmp);

    if (bytes_read != 256)
    {
        Debug_printf("Short read of %u bytes. Exiting.", bytes_read);
        return;
    }

    deviceSpec = std::string(tmp);

    channelMode = PROTOCOL;

    // Delete timer if already extant.
    timer_stop();

    // persist aux1/aux2 values
    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;
    open_aux2 |= trans_aux2;
    cmdFrame.aux2 |= trans_aux2;

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
    ns.reset();

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
        //SYSTEM_BUS.write(ns.error);
        return;
    }

    // Set line ending to CR
    protocol->setLineEnding("\x0D");

    // Attempt protocol open
    if (protocol->open(urlParser.get(), &cmdFrame) == true)
    {
        ns.error = protocol->error;
        Debug_printf("Protocol unable to make connection. Error: %d\n", ns.error);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        //SYSTEM_BUS.write(ns.error);
        return;
    }

    // Everything good, start the interrupt timer!
    timer_start();

    // Go ahead and send an interrupt, so CoCo knows to get ns.
    protocol->forceStatus = true;

    // TODO: Finally, go ahead and let the parsers know
    json = new FNJSON();
    json->setLineEnding("\x0a");
    json->setProtocol(protocol);
    channelMode = PROTOCOL;

    // And signal complete!
    ns.error = 1;
    //SYSTEM_BUS.write(ns.error);
    Debug_printf("ns.error = %u\n",ns.error);
}

/**
 * DRIVEWIRE Close command
 * Tear down everything set up by drivewire_open(), as well as RX interrupt.
 */
void drivewireNetwork::close()
{
    Debug_printf("drivewireNetwork::sio_close()\n");

    ns.reset();

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        //SYSTEM_BUS.write(ns.error);
        return;
    }

    // Ask the protocol to close
    protocol->close();

#ifdef ESP_PLATFORM
    Debug_printv("Before protocol delete %lu\n",esp_get_free_internal_heap_size());
#endif
    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    if (json != nullptr)
    {
        delete json;
        json = nullptr;
    }

#ifdef ESP_PLATFORM
    Debug_printv("After protocol delete %lu\n",esp_get_free_internal_heap_size());
#endif

    //SYSTEM_BUS.write(ns.error);
}

/**
 * DRIVEWIRE Read command
 * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
 * fill the rest with nulls and return ERROR.
 *
 * @note It is the channel's responsibility to pad to required length.
 */
void drivewireNetwork::read()
{
    uint8_t num_bytesh = cmdFrame.aux1;
    uint8_t num_bytesl = cmdFrame.aux2;
    uint16_t num_bytes = (num_bytesh * 256) + num_bytesl;

    if (!num_bytes)
    {
        Debug_printf("drivewireNetwork::read() - Zero bytes requested. Bailing.\n");
        return;
    }

    Debug_printf("drivewireNetwork::read( %u bytes)\n", num_bytes);

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        ns.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
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

        ns.error = NETWORK_ERROR_NOT_CONNECTED;
        return;
    }

    // Do the channel read
    read_channel(num_bytes);

    // And set response buffer.
    response += *receiveBuffer;

    // Remove from receive buffer and shrink.
    receiveBuffer->erase(0, num_bytes);
    receiveBuffer->shrink_to_fit();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
bool drivewireNetwork::read_channel_json(unsigned short num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining = 0;
    else
        json_bytes_remaining -= num_bytes;

    return false;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool drivewireNetwork::read_channel(unsigned short num_bytes)
{
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->read(num_bytes);
        break;
    case JSON:
        err = read_channel_json(num_bytes);
        break;
    }
    return err;
}

/**
 * DRIVEWIRE Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to DRIVEWIRE. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void drivewireNetwork::write()
{
    uint16_t num_bytes = get_daux();
    char *txbuf=nullptr;

    if (!num_bytes)
    {
        Debug_printf("drivewireNetwork::write() - refusing to write 0 bytes.\n");
        return;
    }

    txbuf=(char *)malloc(num_bytes);

    if (!txbuf)
    {
        Debug_printf("drivewireNetwork::write() - could not allocate %u bytes.\n", num_bytes);
        return;
    }

    if (SYSTEM_BUS.read((uint8_t *)txbuf, num_bytes) < num_bytes)
    {
        Debug_printf("drivewireNetwork::write() - short read\n");
        free(txbuf);
        return;
    }

    Debug_printf("sioNetwork::drivewire_write( %u bytes)\n", num_bytes);

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        ns.error = NETWORK_ERROR_NOT_CONNECTED;
        return;
    }

    std::string s = std::string(txbuf,num_bytes);

    *transmitBuffer += s;

    free(txbuf);

    // Do the channel write
    write_channel(num_bytes);
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit drivewire_error or drivewire_complete().
 */
bool drivewireNetwork::write_channel(unsigned short num_bytes)
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
 * DRIVEWIRE Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to DRIVEWIRE.
 */
void drivewireNetwork::status()
{
    if (protocol == nullptr)
        status_local();
    else
        status_channel();
}

/**
 * @brief perform local status commands, if protocol is not bound, based on cmdFrame
 * value.
 */
void drivewireNetwork::status_local()
{
    uint8_t ipAddress[4];
    uint8_t ipNetmask[4];
    uint8_t ipGateway[4];
    uint8_t ipDNS[4];
    NDeviceStatus status {};


    Debug_printf("drivewireNetwork::sio_status_local(%u)\n", cmdFrame.aux2);

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (cmdFrame.aux2)
    {
    case 1: // IP Address
        Debug_printf("IP Address: %u.%u.%u.%u\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
        memcpy(&status,ipAddress,sizeof(status));
        break;
    case 2: // Netmask
        Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
        memcpy(&status,ipNetmask,sizeof(status));
        break;
    case 3: // Gatway
        Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
        memcpy(&status,ipGateway,sizeof(status));
        break;
    case 4: // DNS
        Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
        memcpy(&status,ipDNS,sizeof(status));
        break;
    default:
        status.conn = ns.connected;
        status.err = ns.error;
        break;
    }

    response.clear();
    response.append((char *) &status, sizeof(status));
}

bool drivewireNetwork::status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? 1 : 136;
    ns->rxBytesWaiting = json_bytes_remaining;
    return false; // for now
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void drivewireNetwork::status_channel()
{
    NDeviceStatus status;

    Debug_printf("drivewireNetwork::sio_status_channel(%u)\n", channelMode);

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status_channel on a null protocol.\r\n");
            ns.error = true;
        } else {
            protocol->status(&ns);
        }
        break;
    case JSON:
        status_channel_json(&ns);
        break;
    }
    // clear forced flag (first status after open)
    protocol->forceStatus = false;

    // Serialize status into status bytes (rxBytesWaiting sent big endian!)
    size_t avail = ns.rxBytesWaiting;
    avail = avail > 65535 ? 65535 : avail;
    status.avail = htobe16(avail);
    status.conn = ns.connected;
    status.err = ns.error;

#if 1 //def TOO_MUCH_DEBUG
    Debug_printf("status_channel() - BW: %u C: %u E: %u\n",
                 avail, ns.connected, ns.error);
#endif /* TOO_MUCH_DEBUG */

    // and fill response.
    response.clear();
    response.shrink_to_fit();
    response.append((char *) &status, sizeof(status));
}

/**
 * Get Prefix
 */
void drivewireNetwork::get_prefix()
{
    char out[256];
    Debug_printf("drivewireNetwork::get_prefix(%s)\n",prefix.c_str());
    memset(out,0,sizeof(out));
    strcpy(out,prefix.c_str());
    response = std::string(out,256);
}

/**
 * Set Prefix
 */
void drivewireNetwork::set_prefix()
{
    std::string prefixSpec_str;
    char tmp[256];
    memset(tmp,0,sizeof(tmp));
    size_t read_bytes = SYSTEM_BUS.read((uint8_t *)tmp, 256);

    if (read_bytes != 256)
    {
        Debug_printf("Short read by %u bytes. Exiting.", read_bytes);
        return;
    }

    prefixSpec_str = string((const char *)tmp);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("sioNetwork::sio_set_prefix(%s)\n", prefixSpec_str.c_str());

    // If "NCD Nn:" then prefix is cleared completely
    if (prefixSpec_str.empty())
    {
        prefix.clear();
    }
    else
    {
        // For the remaining cases, append trailing slash if not found
        if (prefix[prefix.size()-1] != '/')
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
            // Check for trailing slash. Append if missing.
            if (prefix[prefix.size()-1] != '/')
            {
                prefix += "/";
            }
        }
        else // append to path.
        {
            prefix += prefixSpec_str;
        }
    }

    prefix = util_get_canonical_path(prefix);
    Debug_printf("Prefix now: %s\n", prefix.c_str());

}

/**
 * @brief set channel mode
 */
void drivewireNetwork::set_channel_mode()
{
    switch (cmdFrame.aux1)
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

    Debug_printv("channel mode now %u\n",channelMode);
}

/**
 * Set login
 */
void drivewireNetwork::set_login()
{
    char tmp[256];
    memset(tmp,0,sizeof(tmp));

    size_t bytes_read = SYSTEM_BUS.read((uint8_t *)tmp, 256);

    if (bytes_read != 256)
    {
        Debug_printf("Short read of %u bytes. Exiting.\n", bytes_read);
        return;
    }

    login = std::string(tmp,256);

    Debug_printf("drivewireNetwork::set_login(%s)\n",login.c_str());
}

/**
 * Set password
 */
void drivewireNetwork::set_password()
{
    char tmp[256];
    memset(tmp,0,sizeof(tmp));

    size_t bytes_read = SYSTEM_BUS.read((uint8_t *)tmp, 256);

    if (bytes_read != 256)
    {
        Debug_printf("Short read of %u bytes. Exiting.\n", bytes_read);
        return;
    }

    password = std::string(tmp,256);

    Debug_printf("drivewireNetwork::set_password(%s)\n", password.c_str());
}

/**
 * DRIVEWIRE Special, called as a default for any other DRIVEWIRE command not processed by the other drivewire_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either drivewire_complete()
 * or drivewire_error() is called.
 */
void drivewireNetwork::special()
{
    do_inquiry((fujiCommandID_t) cmdFrame.comnd);

    switch (inq_dstats)
    {
    case 0x00: // No payload
        special_00();
        break;
    case 0x40: // Payload to Atari
        special_40();
        break;
    case 0x80: // Payload to Peripheral
        special_80();
        break;
    default:
        break;
    }
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: DRIVEWIRE call.
 */
void drivewireNetwork::special_inquiry()
{
    Debug_printf("drivewireNetwork::special_inquiry(%02x)\n", cmdFrame.aux1);

    do_inquiry((fujiCommandID_t) cmdFrame.aux1);

    // Finally, return the completed inq_dstats value back to CoCo
    SYSTEM_BUS.write(&inq_dstats, sizeof(inq_dstats));
}

void drivewireNetwork::do_inquiry(fujiCommandID_t inq_cmd)
{
    // Reset inq_dstats
    inq_dstats = 0xff;

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
    {
        inq_dstats = protocol->special_inquiry(inq_cmd);
        Debug_printf("protocol special_inquiry returned %d\r\n", inq_dstats);
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

    Debug_printf("inq_dstats = %u\n", inq_dstats);
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal drivewire_complete() or error().
 */
void drivewireNetwork::special_00()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case 'P':
        if (channelMode == JSON)
            parse_json();
        break;
    case 'T':
        set_translation();
        break;
    case 0xFC: // SET CHANNEL MODE
        set_channel_mode();
        break;
    default:
        protocol->special_00(&cmdFrame);
    }

}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void drivewireNetwork::special_40()
{
    // Handle commands that exist outside of an open channel.
    switch (cmdFrame.comnd)
    {
    case 0x30:
        get_prefix();
        return;
    }

    // not sure what to do here, FIXME.
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void drivewireNetwork::special_80()
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
        do_idempotent_command_80();
        return;
    case 0x2C: // CHDIR   ','
        set_prefix();
        return;
    case 'Q':
        if (channelMode == JSON)
            json_query();
        return;
    case 0xFD: // LOGIN
        set_login();
        return;
    case 0xFE: // PASSWORD
        set_password();
        return;
    }

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer

    SYSTEM_BUS.read(spData,256);

    Debug_printf("drivewireNetwork::special_80() - %s\n", spData);

    if (protocol == nullptr) {
        Debug_printf("ERROR: Calling special_80 on a null protocol.\r\n");
        ns.reset();
        ns.error = true;
        return;
    }

    // Do protocol action and return
    protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame);

    protocol->status(&ns);
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool drivewireNetwork::instantiate_protocol()
{
    if (urlParser == nullptr)
    {
        Debug_printf("drivewireNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
        return false; // error.
    }

    // Convert to uppercase
    transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

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
        Debug_printf("drivewireNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    if (!login.empty())
    {
        protocol->login = &login;
        protocol->password = &password;
    }

    Debug_printf("drivewireNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
 */
void drivewireNetwork::assert_interrupt()
{
#ifdef ESP_PLATFORM
    fnSystem.digital_write(PIN_CD, interruptCD == true ? DIGI_HIGH : DIGI_LOW);
#else
/* TODO: We'll get to this at a future date.

    uint64_t ms = fnSystem.millis();
    if (ms - lastInterruptMs >= timerRate)
    {
        interruptCD = !interruptCD;
        fnSioCom.set_proceed(interruptCD);
        lastInterruptMs = ms;
    }
    */
#endif
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed (continue toggling PROCEED).
 */
void drivewireNetwork::poll_interrupt()
{
    if (protocol != nullptr)
    {
        if (protocol->interruptEnable == false)
            return;

        /* assert interrupt if we need Status call from host to arrive */
        if (protocol->forceStatus == true)
        {
            assert_interrupt();
            return;
        }

        protocol->fromInterrupt = true;
        protocol->status(&ns);
        protocol->fromInterrupt = false;

        if (ns.rxBytesWaiting > 0 || ns.connected == 0)
            assert_interrupt();
#ifndef ESP_PLATFORM
else
/* TODO: We'll get to this at a future date.
            sio_clear_interrupt();
 */
#endif

        reservedSave = ns.connected;
        errorSave = ns.error;
    }
}

void drivewireNetwork::send_error()
{
    Debug_printf("drivewireNetwork::send_error(%u)\n",ns.error);
    SYSTEM_BUS.write(ns.error);
}

void drivewireNetwork::send_response()
{
    uint16_t len = cmdFrame.aux1 << 8 | cmdFrame.aux2; // big endian

    // Pad to requested response length. Thanks apc!
    if (response.length() < len)
        response.insert(response.length(), len - response.length(), '\0');

    // Send body
    SYSTEM_BUS.write((uint8_t *)response.c_str(), len);

    Debug_printf("drivewireNetwork::send_response[%d]:%s\n", len, response.c_str());

    // Clear the response
    response.clear();
    response.shrink_to_fit();
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void drivewireNetwork::create_devicespec()
{
    // Get Devicespec from buffer, and put into primary devicespec string

    deviceSpec = util_devicespec_fix_for_parsing(deviceSpec, prefix, cmdFrame.aux1 == 6, true);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void drivewireNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void drivewireNetwork::parse_and_instantiate_protocol()
{
    create_devicespec();
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        ns.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
        ns.error = NETWORK_ERROR_GENERAL;
        return;
    }
}

/**
 * Is this a valid URL? (Used to generate ERROR 165)
 */
bool drivewireNetwork::isValidURL(PeoplesUrlParser *url)
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
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of drivewireNetwork.
 *
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool drivewireNetwork::parseURL()
{
    string url;
    string unit = deviceSpec.substr(0, deviceSpec.find_first_of(":") + 1);

    // Prepend prefix, if set.
    if (prefix.length() > 0)
        deviceSpec = unit + prefix + deviceSpec.substr(deviceSpec.find(":") + 1);
    else
        deviceSpec = unit + deviceSpec.substr(string(deviceSpec).find(":") + 1);

    Debug_printf("drivewireNetwork::parseURL(%s)\n", deviceSpec.c_str());

    // Strip non-ascii characters.
    util_strip_nonascii(deviceSpec);

    // Process comma from devicespec (DOS 2 COPY command)
    // processCommaFromDevicespec();

    if (cmdFrame.aux1 != 6) // Anything but a directory read...
    {
        replace(deviceSpec.begin(), deviceSpec.end(), '*', '\0'); // FIXME: Come back here and deal with WC's
    }

    // Some FMSes add a dot at the end, remove it.
    if (deviceSpec.substr(deviceSpec.length() - 1) == ".")
        deviceSpec.erase(deviceSpec.length() - 1, string::npos);

    // Remove any spurious spaces
    deviceSpec = util_remove_spaces(deviceSpec);

    // chop off front of device name for URL, and parse it.
    url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);

    Debug_printf("drivewireNetwork::parseURL transformed to (%s, %s)\n", deviceSpec.c_str(), url.c_str());

    return isValidURL(urlParser.get());
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
void drivewireNetwork::processCommaFromDevicespec()
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

void drivewireNetwork::set_translation()
{
    trans_aux2 = cmdFrame.aux2;
}

void drivewireNetwork::parse_json()
{
    ns.error = json->parse() ? NETWORK_ERROR_SUCCESS : NETWORK_ERROR_COULD_NOT_PARSE_JSON;
}

void drivewireNetwork::json_query()
{
    std::string in_string;
    char tmpq[256];
    memset(tmpq,0,sizeof(tmpq));

    size_t bytes_read = SYSTEM_BUS.read((uint8_t *)tmpq,256);

    // why does it need to be 256 bytes?
    if (bytes_read != 256)
    {
        Debug_printf("Short read of %u bytes. Exiting\n", bytes_read);
        return;
    }

    in_string = std::string(tmpq,256);

    // strip away line endings from input spec.
    for (int i = 0; i < in_string.size(); i++)
    {
        unsigned char currentChar = static_cast<unsigned char>(in_string[i]);
        if (currentChar == 0x0A || currentChar == 0x0D || currentChar == 0x9b)
        {
            in_string[i] = '\0';
        }
    }

    // Query param is only used in ATARI at the moment, and 256 is too large for the type.
    json->setReadQuery(in_string, 0);
    json_bytes_remaining = json->json_bytes_remaining;

    std::vector<uint8_t> tmp(json_bytes_remaining);
    json->readValue(tmp.data(), json_bytes_remaining);

    // don't copy past first nul char in tmp
    auto null_pos = std::find(tmp.begin(), tmp.end(), 0);
    *receiveBuffer += std::string(tmp.begin(), null_pos);

    for (int i=0;i<in_string.length();i++)
        Debug_printf("%02X ",(unsigned char)in_string[i]);

    Debug_printf("\n");

    Debug_printf("Query set to >%s<\r\n", in_string.c_str());
}

void drivewireNetwork::do_idempotent_command_80()
{
    Debug_printf("sioNetwork::sio_do_idempotent_command_80()\r\n");
// #ifdef ESP_PLATFORM // apc: isn't it already ACK'ed?
//     sio_ack();
// #endif

    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        Debug_printf("Protocol = NULL\n");
        //sio_error();
        return;
    }

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame) == true)
    {
        Debug_printf("perform_idempotent_80 failed\n");
        // sio_error();
    }
    // else
    //     sio_complete();
}

void drivewireNetwork::process()
{
    // Read the three command and aux bytes
    cmdFrame.comnd = (uint8_t)SYSTEM_BUS.read();
    cmdFrame.aux1 = (uint8_t)SYSTEM_BUS.read();
    cmdFrame.aux2 = (uint8_t)SYSTEM_BUS.read();

    Debug_printf("comnd: '%c' %u,%u,%u\n",cmdFrame.comnd,cmdFrame.comnd,cmdFrame.aux1,cmdFrame.aux2);

    switch (cmdFrame.comnd)
    {
    case 0x00: // Ready?
        ready(); // Yes.
        break;
    case 0x01: // Send Response
        send_response();
        break;
    case 0x02: // Send error
        send_error();
        break;
    case 'O':
        open();
        break;
    case 'C':
        close();
        break;
    case 'R':
        read();
        break;
    case 'W':
        write();
        break;
    case 'S':
        status();
        break;
    case 0xFF:
        special_inquiry();
        break;
    default:
        special();
        break;
    }
}

#endif /* BUILD_COCO */
