/**
 * N: Firmware
*/
#include <string.h>
#include <algorithm>
#include <vector>
#include "utils.h"
#include "network.h"
#include "../hardware/fnSystem.h"
#include "../hardware/fnWiFi.h"

using namespace std;

/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
 * flag to true. This is set to false when the interrupt is serviced.
 */
void onTimer(void *info)
{
    sioNetwork *parent = (sioNetwork *)info;
    portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptProceed = !parent->interruptProceed;
    portEXIT_CRITICAL_ISR(&parent->timerMux);
}

/**
 * Constructor
 */
sioNetwork::sioNetwork()
{
}

/**
 * Destructor
 */
sioNetwork::~sioNetwork()
{
}

/** SIO COMMANDS ***************************************************************/

/**
 * SIO Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void sioNetwork::sio_open()
{
    uint8_t devicespecBuf[256];

    Debug_println("sioNetwork::sio_open()\n");

    sio_ack();

    channelMode = PROTOCOL;

    // Delete timer if already extant.
    timer_stop();

    // persist aux1/aux2 values
    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;
    open_aux2 |= trans_aux2;

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        delete protocol;
        free_buffers();
    }

    // Reset status buffer
    status.reset();

    // Get Devicespec from buffer, and put into primary devicespec string
    sio_to_peripheral(devicespecBuf, sizeof(devicespecBuf));
    deviceSpec = string((char *)devicespecBuf);

    // Invalid URL returns error 165 in status.
    if (parseURL() == false)
    {
        Debug_printf("Invalid devicespec: %s", deviceSpec.c_str());
        status.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        sio_error();
        return;
    }

    Debug_printf("Open: %s\n", deviceSpec.c_str());

    // Attempt to allocate buffers
    if (allocate_buffers() == false)
    {
        Debug_printf("Could not allocate memory for buffers\n");
        status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        sio_error();
        return;
    }

    // Instantiate protocol object.
    if (instantiate_protocol() == false)
    {
        Debug_printf("Could not open protocol.\n");
        status.error = NETWORK_ERROR_GENERAL;
        sio_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser, &cmdFrame) == false)
    {
        Debug_printf("Protocol unable to make connection.\n");
        protocol->close();
        delete protocol;
        protocol = nullptr;
        status.error = NETWORK_ERROR_CONNECTION_REFUSED;
        sio_error();
        return;
    }

    // Everything good, start the interrupt timer!
    timer_start();

    // TODO: Finally, go ahead and let the parsers know

    // And signal complete!
    sio_complete();
}

/**
 * SIO Close command
 * Tear down everything set up by sio_open(), as well as RX interrupt.
 */
void sioNetwork::sio_close()
{
    Debug_printf("sioNetwork::sio_close()\n");

    sio_ack();

    status.reset();

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        sio_complete();
        return;
    }

    // Ask the protocol to close
    if (protocol->close())
        sio_complete();
    else
        sio_error();

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;

    // And deallocate buffers
    free_buffers();
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

    Debug_printf("sioNetwork::sio_read( %d bytes)\n", num_bytes);

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
        status.error = NETWORK_ERROR_NOT_CONNECTED;
        sio_error();
        return;
    }

    // Clean out RX buffer
    memset(receiveBuffer, 0, INPUT_BUFFER_SIZE);

    // Do the channel read
    err = sio_read_channel(num_bytes);

    // And send off to the computer
    sio_to_computer(receiveBuffer, num_bytes, err);
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to sio_to_computer().
 */
bool sioNetwork::sio_read_channel(unsigned short num_bytes)
{
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        err = protocol->read(receiveBuffer, num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        err = true;
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
    uint8_t ck;

    Debug_printf("sioNetwork::sio_write( %d bytes)\n", num_bytes);

    sio_ack();

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (transmitBuffer == nullptr)
    {
        status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        sio_error();
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        status.error = NETWORK_ERROR_NOT_CONNECTED;
        sio_error();
        return;
    }

    // Clean out RX buffer
    memset(transmitBuffer, 0, OUTPUT_BUFFER_SIZE);

    // Get the data from the Atari
    ck = sio_to_peripheral(transmitBuffer, num_bytes);

    if (ck != cmdFrame.checksum)
    {
        Debug_printf("Checksum Mismatch!, sending ERROR.\n");
        sio_error();
        return;
    }

    // Do the channel write
    err = sio_write_channel(num_bytes);

    // Acknowledge to Atari of channel outcome.
    if (err == false)
        sio_complete();
    else
        sio_error();
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
        err = protocol->write(transmitBuffer, num_bytes);
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

    fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    switch (cmdFrame.aux2)
    {
    case 1: // IP Address
        sio_to_computer(ipAddress, 4, false);
        break;
    case 2: // Netmask
        sio_to_computer(ipNetmask, 4, false);
        break;
    case 3: // Gatway
        sio_to_computer(ipGateway, 4, false);
        break;
    case 4: // DNS
        sio_to_computer(ipDNS, 4, false);
        break;
    default:
        default_status[3] = fnWiFi.connected();
        sio_to_computer(default_status, 4, false);
    }
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void sioNetwork::sio_status_channel()
{
    uint8_t serialized_status[4] = {0, 0, 0, 0};
    bool err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        // err=protocol->status(&status);
        break;
    case JSON:
        // err=_json->status(&status)
        break;
    }

    // Serialize status into status bytes
    serialized_status[0] = status.rxBytesWaiting & 0xFF;
    serialized_status[1] = status.rxBytesWaiting >> 8;
    serialized_status[2] = status.reserved;
    serialized_status[3] = status.error;

    // and send to computer
    sio_to_computer(serialized_status, sizeof(serialized_status), err);
}

/**
 * SIO Special, called as a default for any other SIO command not processed by the other sio_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either sio_complete()
 * or sio_error() is called.
 */
void sioNetwork::sio_special()
{
    uint8_t inq_cmd = sio_get_aux();
    uint8_t inq_dstats = 0xFF;

    // If there's no protocol, and we got here, then we nak the command, because it's not valid.
    if (protocol == nullptr)
    {
        Debug_printf("Attempted to do undefined special command with no protocol.\n");
        sio_nak();
        return;
    }

    // Ask protocol for DSTATS value.
    inq_dstats = protocol->special_inquiry(inq_cmd);

    if (inq_dstats == 0xFF)
    {
        Debug_printf("Attempted to do undefined special command to protocol.\n");
        sio_nak();
        return;
    }

    switch (inq_dstats)
    {
    case 0x00: // No payload
        sio_special_protocol_00();
        break;
    case 0x40: // Payload to Atari
        sio_special_protocol_40();
        break;
    case 0x80: // Payload to Peripheral
        sio_special_protocol_80();
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
    uint8_t inq_cmd = cmdFrame.aux1;
    uint8_t inq_dstats = 0xFF;

    // Acknowledge
    sio_ack();

    Debug_printf("sioNetwork::sio_special_inquiry(%02x)\n", inq_cmd);

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
        inq_dstats = protocol->special_inquiry(inq_cmd);

    // If we didn't get one from protocol, or unsupported, see if supported globally.
    if (inq_dstats == 0xFF)
    {
        switch (inq_cmd)
        {
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

    // Finally, return the completed inq_dstats value back to Atari
    sio_to_computer(&inq_dstats, sizeof(inq_dstats), false); // never errors.
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action 
 * and based on the return, signal sio_complete() or error().
 */
void sioNetwork::sio_special_protocol_00()
{
    if (protocol->special_00(&cmdFrame) == false)
        sio_complete();
    else
        sio_error();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use sio_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void sioNetwork::sio_special_protocol_40()
{
    memset(receiveBuffer, 0, INPUT_BUFFER_SIZE);
    sio_to_computer(receiveBuffer,
                    SPECIAL_BUFFER_SIZE,
                    protocol->special_40(receiveBuffer, SPECIAL_BUFFER_SIZE, &cmdFrame));
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use sio_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void sioNetwork::sio_special_protocol_80()
{
    uint8_t ck;
    memset(transmitBuffer, 0, OUTPUT_BUFFER_SIZE);

    // Get special (devicespec) from computer
    ck = sio_to_peripheral(transmitBuffer, 256);

    // Bomb if checksum mismatch.
    if (ck != cmdFrame.checksum)
    {
        Debug_printf("sioNetwork::sio_special_protocol_80() - mismatched checksum\n");
        sio_error();
        return;
    }

    // Do protocol action and return
    if (protocol->special_80(transmitBuffer, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
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

    Debug_printf("sioNetwork::sio_process 0x%02hx '%c': 0x%02hx, 0x%02hx\n",
                 cmdFrame.comnd, cmdFrame.comnd, cmdFrame.aux1, cmdFrame.aux2);

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
 * Check to see if PROCEED needs to be asserted, and assert if needed.
 */
void sioNetwork::sio_poll_interrupt()
{
    if ((protocol != nullptr) && (interruptProceed == true))
    {
        protocol->status(&status);

        if (lastNetworkStatusChecksum != status.checksum())
            sio_assert_interrupt();

        lastNetworkStatusChecksum = status.checksum();
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Allocate rx and tx buffers
 * @return bool TRUE if ok, FALSE if in error.
 */
bool sioNetwork::allocate_buffers()
{
    receiveBuffer = (uint8_t *)heap_caps_malloc(INPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    transmitBuffer = (uint8_t *)heap_caps_malloc(OUTPUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if ((receiveBuffer == nullptr) || (transmitBuffer == nullptr))
        return false; // Allocation failed.

    /* Clear buffer and status */
    status.reset();
    memset(receiveBuffer, 0, INPUT_BUFFER_SIZE);
    memset(transmitBuffer, 0, OUTPUT_BUFFER_SIZE);

    HEAP_CHECK("sioNetwork::allocate_buffers");
    return true; // All good.
}

/**
 * Free the rx and tx buffers
 */
void sioNetwork::free_buffers()
{
    if (receiveBuffer != nullptr)
        free(receiveBuffer);
    if (transmitBuffer != nullptr)
        free(transmitBuffer);

    Debug_printf("sioNetworks::free_buffers()\n");
}

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool sioNetwork::instantiate_protocol()
{
    if (urlParser == nullptr)
    {
        Debug_printf("sioNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
        return false; // error.
    }

    // Convert to uppercase
    transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

    if (urlParser->scheme == "TCP")
    {
        //protocol = new networkProtocolTCP();
    }
    else if (urlParser->scheme == "UDP")
    {
        //protocol = new networkProtocolUDP();
        // TODO: Change NetworkProtocolUDP to pass saved RX buffer into ctor!
    }
    else if (urlParser->scheme == "HTTP" || urlParser->scheme == "HTTPS")
    {
        //protocol = new networkProtocolHTTP();
    }
    else if (urlParser->scheme == "TNFS")
    {
        //protocol = new networkProtocolTNFS();
    }
    else if (urlParser->scheme == "FTP")
    {
        //protocol = new networkProtocolFTP();
    }
    else
    {
        return false; // invalid protocol.
    }

    if (protocol == nullptr)
    {
        Debug_printf("sioNetwork::open_protocol() - Could not open protocol.\n");
        return false;
    }

    Debug_printf("sioNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Start the Interrupt rate limiting timer
 */
void sioNetwork::timer_start()
{
    esp_timer_create_args_t tcfg;
    tcfg.arg = this;
    tcfg.callback = onTimer;
    tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    tcfg.name = nullptr;
    esp_timer_create(&tcfg, &rateTimerHandle);
    esp_timer_start_periodic(rateTimerHandle, 100000); // 100ms
}

/**
 * Stop the Interrupt rate limiting timer
 */
void sioNetwork::timer_stop()
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

/**
 * Is this a valid URL? (Used to generate ERROR 165)
 */
bool sioNetwork::isValidURL(EdUrlParser *url)
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
 * of sioNetwork.
 * 
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool sioNetwork::parseURL()
{
    string url;

    if (urlParser != nullptr)
        delete urlParser;

    Debug_printf("sioNetwork::parseURL(%s)\n", deviceSpec.c_str());

    // Strip non-ascii characters.
    util_strip_nonascii(deviceSpec);

    // Process comma from devicespec (DOS 2 COPY command)
    processCommaFromDevicespec();

    if (cmdFrame.aux1 != 6) // Anything but a directory read...
    {
        replace(deviceSpec.begin(), deviceSpec.end(), '*', '\0'); // FIXME: Come back here and deal with WC's
    }

    // Some FMSes add a dot at the end, remove it.
    if (deviceSpec.substr(-1) == ".")
        deviceSpec[deviceSpec.length() - 1] = 0x00;

    // Prepend prefix, if set.
    if (prefix.length() > 0)
        deviceSpec = prefix + deviceSpec.substr(deviceSpec.find(":") + 1);

    // Remove any spurious spaces
    deviceSpec = util_remove_spaces(deviceSpec);

    // chop off front of device name for URL, and parse it.
    url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = EdUrlParser::parseUrl(url);

    Debug_printf("sioNetwork::parseURL transformed to (%s, %s)", deviceSpec.c_str(), url);

    return isValidURL(urlParser);
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

    Debug_printf("Passed back deviceSpec %s\n", deviceSpec);
}

/**
 * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
 */
void sioNetwork::sio_assert_interrupt()
{
    // Pulse Interrupt for 50μs
    fnSystem.digital_write(PIN_PROC, DIGI_LOW);
    fnSystem.delay_microseconds(50);
    fnSystem.digital_write(PIN_PROC, DIGI_HIGH);
}
