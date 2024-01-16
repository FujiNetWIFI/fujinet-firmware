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

using namespace std;

/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
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

/**
 * DRIVEWIRE Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void drivewireNetwork::open()
{
    Debug_printf("drivewireNetwork::sio_open(%02x,%02x)\n",cmdFrame.aux1,cmdFrame.aux2);

    while (fnUartBUS.available())
        deviceSpec += fnUartBUS.read();

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
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser, &cmdFrame) == true)
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

        return;
    }

    // Everything good, start the interrupt timer!
    timer_start();

    // Go ahead and send an interrupt, so CoCo knows to get status.
    protocol->forceStatus = true;

    // TODO: Finally, go ahead and let the parsers know
    json = new FNJSON();
    json->setLineEnding("\x0a");
    json->setProtocol(protocol);
    channelMode = PROTOCOL;

    // And signal complete!
    fnUartBUS.write(ns.error);
}

/**
 * DRIVEWIRE Close command
 * Tear down everything set up by drivewire_open(), as well as RX interrupt.
 */
void drivewireNetwork::close()
{
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
}

/**
 * Get Prefix
 */
void drivewireNetwork::get_prefix()
{
}

/**
 * Set Prefix
 */
void drivewireNetwork::set_prefix()
{
}

/**
 * @brief set channel mode
 */
void drivewireNetwork::set_channel_mode()
{
    switch (cmdFrame.aux2)
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

/**
 * Set login
 */
void drivewireNetwork::set_login()
{
}

/**
 * Set password
 */
void drivewireNetwork::set_password()
{
}

/**
 * DRIVEWIRE Special, called as a default for any other DRIVEWIRE command not processed by the other drivewire_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either drivewire_complete()
 * or drivewire_error() is called.
 */
void drivewireNetwork::special()
{
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: DRIVEWIRE call.
 */
void drivewireNetwork::special_inquiry()
{
}

void drivewireNetwork::do_inquiry(unsigned char inq_cmd)
{
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal drivewire_complete() or error().
 */
void drivewireNetwork::special_00()
{
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void drivewireNetwork::special_40()
{
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void drivewireNetwork::special_80()
{
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

void drivewireNetwork::parse_and_instantiate_protocol()
{
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
 * The resulting URL is then sent into EdURLParser to get our URLParser object which is used in the rest
 * of drivewireNetwork.
 *
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool drivewireNetwork::parseURL()
{
    string url;
    string unit = deviceSpec.substr(0, deviceSpec.find_first_of(":") + 1);

    if (urlParser != nullptr)
    {
        delete urlParser;
        urlParser = nullptr;
    }

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
void drivewireNetwork::processCommaFromDevicespec()
{
}

void drivewireNetwork::set_translation()
{
}

void drivewireNetwork::parse_json()
{
}

void drivewireNetwork::json_query()
{
}

void drivewireNetwork::set_timer_rate()
{
}

void drivewireNetwork::do_idempotent_command_80()
{
}

void drivewireNetwork::process()
{
    // Read the three command and aux bytes
    cmdFrame.comnd = (uint8_t)fnUartBUS.read();
    cmdFrame.aux1 = (uint8_t)fnUartBUS.read();
    cmdFrame.aux2 = (uint8_t)fnUartBUS.read();

    switch (cmdFrame.comnd)
    {
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
