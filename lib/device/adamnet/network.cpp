#ifdef BUILD_ADAM

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
#include "../network-protocol/TCP.h"
#include "../network-protocol/UDP.h"
#include "../network-protocol/Test.h"
#include "../network-protocol/Telnet.h"
#include "../network-protocol/TNFS.h"
#include "../network-protocol/FTP.h"
#include "../network-protocol/HTTP.h"
#include "../network-protocol/SSH.h"
#include "../network-protocol/SMB.h"

using namespace std;

/**
 * Static callback function for the interrupt rate limiting timer. It sets the interruptProceed
 * flag to true. This is set to false when the interrupt is serviced.
 */
void onTimer(void *info)
{
    adamNetwork *parent = (adamNetwork *)info;
    portENTER_CRITICAL_ISR(&parent->timerMux);
    parent->interruptProceed = !parent->interruptProceed;
    portEXIT_CRITICAL_ISR(&parent->timerMux);
}

/**
 * Constructor
 */
adamNetwork::adamNetwork()
{
    status_response[4]=0x00; // Character device

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
adamNetwork::~adamNetwork()
{
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

/** ADAM COMMANDS ***************************************************************/

/**
 * ADAM Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void adamNetwork::adamnet_open()
{
    Debug_println("adamNetwork::adamnet_open()\n");

    channelMode = PROTOCOL;

    // Delete timer if already extant.
    timer_stop();

    cmdFrame.aux1 = adamnet_recv();
    cmdFrame.aux2 = adamnet_recv();

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

    // Reset status buffer
    status.byte = 0x00;

    // Parse and instantiate protocol
    parse_and_instantiate_protocol();

    if (protocol == nullptr)
    {
        // invalid devicespec error already passed in.
        adamnet_response_nack();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser, &cmdFrame) == true)
    {
        status.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        delete protocol;
        protocol = nullptr;
        adamnet_response_nack();
        return;
    }

    // Everything good, start the interrupt timer!
    timer_start();

    // Go ahead and send an interrupt, so Atari knows to get status.
    adamnet_assert_interrupt();

    // TODO: Finally, go ahead and let the parsers know

    // And signal complete!
    adamnet_response_ack();
}

/**
 * ADAM Close command
 * Tear down everything set up by adamnet_open(), as well as RX interrupt.
 */
void adamNetwork::adamnet_close()
{
    Debug_printf("adamNetwork::adamnet_close()\n");

    adamnet_response_ack();

    status.byte = 0x00;

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        adamnet_response_ack();
        return;
    }

    // Ask the protocol to close
    if (protocol->close())
        adamnet_response_nack();
    else
        adamnet_response_ack();

    // Delete the protocol object
    delete protocol;
    protocol = nullptr;
}

/**
 * ADAM Read command
 * Read # of bytes from the protocol adapter specified by the aux1/aux2 bytes, into the RX buffer. If we are short
 * fill the rest with nulls and return ERROR.
 *
 * @note It is the channel's responsibility to pad to required length.
 */
void adamNetwork::adamnet_read()
{
    // unsigned short num_bytes = adamnet_get_aux();
    // bool err = false;

    // Debug_printf("adamNetwork::adamnet_read( %d bytes)\n", num_bytes);

    // adamnet_ack();

    // // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    // if (receiveBuffer == nullptr)
    // {
    //     status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
    //     adamnet_error();
    //     return;
    // }

    // // If protocol isn't connected, then return not connected.
    // if (protocol == nullptr)
    // {
    //     status.error = NETWORK_ERROR_NOT_CONNECTED;
    //     adamnet_error();
    //     return;
    // }

    // // Do the channel read
    // err = adamnet_read_channel(num_bytes);

    // // And send off to the computer
    // bus_to_computer((uint8_t *)receiveBuffer->data(), num_bytes, err);
    // receiveBuffer->erase(0, num_bytes);
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool adamNetwork::adamnet_read_channel(unsigned short num_bytes)
{
    // bool err = false;

    // switch (channelMode)
    // {
    // case PROTOCOL:
    //     err = protocol->read(num_bytes);
    //     break;
    // case JSON:
    //     Debug_printf("JSON Not Handled.\n");
    //     err = true;
    //     break;
    // }
    // return err;
    return false;
}

/**
 * ADAM Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to ADAM. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void adamNetwork::adamnet_write()
{
    // unsigned short num_bytes = adamnet_get_aux();
    // uint8_t *newData;
    // bool err = false;

    // newData = (uint8_t *)malloc(num_bytes);
    // Debug_printf("adamNetwork::adamnet_write( %d bytes)\n", num_bytes);

    // if (newData == nullptr)
    // {
    //     Debug_printf("Could not allocate %u bytes.\n", num_bytes);
    // }

    // adamnet_ack();

    // // If protocol isn't connected, then return not connected.
    // if (protocol == nullptr)
    // {
    //     status.error = NETWORK_ERROR_NOT_CONNECTED;
    //     adamnet_error();
    //     return;
    // }

    // // Get the data from the Atari
    // bus_to_peripheral(newData, num_bytes);
    // *transmitBuffer += string((char *)newData, num_bytes);
    // free(newData);

    // // Do the channel write
    // err = adamnet_write_channel(num_bytes);

    // // Acknowledge to Atari of channel outcome.
    // if (err == false)
    // {
    //     adamnet_complete();
    // }
    // else
    //     adamnet_error();
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit adamnet_error or adamnet_complete().
 */
bool adamNetwork::adamnet_write_channel(unsigned short num_bytes)
{
    // bool err = false;

    // switch (channelMode)
    // {
    // case PROTOCOL:
    //     err = protocol->write(num_bytes);
    //     break;
    // case JSON:
    //     Debug_printf("JSON Not Handled.\n");
    //     err = true;
    //     break;
    // }
    // return err;
    return false;
}

/**
 * ADAM Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to ADAM.
 */
void adamNetwork::adamnet_status()
{
    // // Acknowledge
    // adamnet_ack();

    // if (protocol == nullptr)
    //     adamnet_status_local();
    // else
    //     adamnet_status_channel();
}

/**
 * @brief perform local status commands, if protocol is not bound, based on cmdFrame
 * value.
 */
void adamNetwork::adamnet_status_local()
{
    // uint8_t ipAddress[4];
    // uint8_t ipNetmask[4];
    // uint8_t ipGateway[4];
    // uint8_t ipDNS[4];
    // uint8_t default_status[4] = {0, 0, 0, 0};

    // Debug_printf("adamNetwork::adamnet_status_local(%u)\n", cmdFrame.aux2);

    // fnSystem.Net.get_ip4_info((uint8_t *)ipAddress, (uint8_t *)ipNetmask, (uint8_t *)ipGateway);
    // fnSystem.Net.get_ip4_dns_info((uint8_t *)ipDNS);

    // switch (cmdFrame.aux2)
    // {
    // case 1: // IP Address
    //     Debug_printf("IP Address: %u.%u.%u.%u\n", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
    //     bus_to_computer(ipAddress, 4, false);
    //     break;
    // case 2: // Netmask
    //     Debug_printf("Netmask: %u.%u.%u.%u\n", ipNetmask[0], ipNetmask[1], ipNetmask[2], ipNetmask[3]);
    //     bus_to_computer(ipNetmask, 4, false);
    //     break;
    // case 3: // Gatway
    //     Debug_printf("Gateway: %u.%u.%u.%u\n", ipGateway[0], ipGateway[1], ipGateway[2], ipGateway[3]);
    //     bus_to_computer(ipGateway, 4, false);
    //     break;
    // case 4: // DNS
    //     Debug_printf("DNS: %u.%u.%u.%u\n", ipDNS[0], ipDNS[1], ipDNS[2], ipDNS[3]);
    //     bus_to_computer(ipDNS, 4, false);
    //     break;
    // default:
    //     default_status[2] = status.connected;
    //     default_status[3] = status.error;
    //     bus_to_computer(default_status, 4, false);
    // }
}

/**
 * @brief perform channel status commands, if there is a protocol bound.
 */
void adamNetwork::adamnet_status_channel()
{
    // uint8_t serialized_status[4] = {0, 0, 0, 0};
    // bool err = false;

    // Debug_printf("adamNetwork::adamnet_status_channel(%u)\n", channelMode);

    // switch (channelMode)
    // {
    // case PROTOCOL:
    //     Debug_printf("PROTOCOL\n");
    //     err = protocol->status(&status);
    //     break;
    // case JSON:
    //     // err=_json->status(&status)
    //     break;
    // }

    // // Serialize status into status bytes
    // serialized_status[0] = status.rxBytesWaiting & 0xFF;
    // serialized_status[1] = status.rxBytesWaiting >> 8;
    // serialized_status[2] = status.connected;
    // serialized_status[3] = status.error;

    // Debug_printf("adamnet_status_channel() - BW: %u C: %u E: %u\n",
    //     status.rxBytesWaiting, status.connected ,status.error);

    // // and send to computer
    // bus_to_computer(serialized_status, sizeof(serialized_status), err);
}

/**
 * Get Prefix
 */
void adamNetwork::adamnet_get_prefix()
{
    // uint8_t prefixSpec[256];
    // string prefixSpec_str;

    // memset(prefixSpec, 0, sizeof(prefixSpec));
    // memcpy(prefixSpec, prefix.data(), prefix.size());

    // prefixSpec[prefix.size()] = 0x9B; // add EOL.

    // bus_to_computer(prefixSpec, sizeof(prefixSpec), false);
}

/**
 * Set Prefix
 */
void adamNetwork::adamnet_set_prefix()
{
    // uint8_t prefixSpec[256];
    // string prefixSpec_str;

    // memset(prefixSpec, 0, sizeof(prefixSpec));

    // bus_to_peripheral(prefixSpec, sizeof(prefixSpec));
    // util_clean_devicespec(prefixSpec, sizeof(prefixSpec));

    // prefixSpec_str = string((const char *)prefixSpec);
    // prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    // Debug_printf("adamNetwork::adamnet_set_prefix(%s)\n", prefixSpec_str.c_str());

    // if (prefixSpec_str == "..") // Devance path N:..
    // {
    //     vector<int> pathLocations;
    //     for (int i = 0; i < prefix.size(); i++)
    //     {
    //         if (prefix[i] == '/')
    //         {
    //             pathLocations.push_back(i);
    //         }
    //     }

    //     if (prefix[prefix.size() - 1] == '/')
    //     {
    //         // Get rid of last path segment.
    //         pathLocations.pop_back();
    //     }

    //     // truncate to that location.
    //     prefix = prefix.substr(0, pathLocations.back() + 1);
    // }
    // else if (prefixSpec_str[0] == '/') // N:/DIR
    // {
    //     prefix = prefixSpec_str;
    // }
    // else if (prefixSpec_str.empty())
    // {
    //     prefix.clear();
    // }
    // else if (prefixSpec_str.find_first_of(":") != string::npos)
    // {
    //     prefix = prefixSpec_str;
    // }
    // else // append to path.
    // {
    //     prefix += prefixSpec_str;
    // }

    // Debug_printf("Prefix now: %s\n", prefix.c_str());

    // // We are okay, signal complete.
    // adamnet_complete();
}

/**
 * Set login
 */
void adamNetwork::adamnet_set_login()
{
    // uint8_t loginSpec[256];

    // memset(loginSpec,0,sizeof(loginSpec));
    // bus_to_peripheral(loginSpec,sizeof(loginSpec));
    // util_clean_devicespec(loginSpec,sizeof(loginSpec));

    // login = string((char *)loginSpec);
    // adamnet_complete();
}

/**
 * Set password
 */
void adamNetwork::adamnet_set_password()
{
    // uint8_t passwordSpec[256];

    // memset(passwordSpec,0,sizeof(passwordSpec));
    // bus_to_peripheral(passwordSpec,sizeof(passwordSpec));
    // util_clean_devicespec(passwordSpec,sizeof(passwordSpec));

    // password = string((char *)passwordSpec);
    // adamnet_complete();
}

/**
 * ADAM Special, called as a default for any other ADAM command not processed by the other adamnet_ functions.
 * First, the protocol is asked whether it wants to process the command, and if so, the protocol will
 * process the special command. Otherwise, the command is handled locally. In either case, either adamnet_complete()
 * or adamnet_error() is called.
 */
void adamNetwork::adamnet_special()
{
    // do_inquiry(cmdFrame.comnd);

    // switch (inq_dstats)
    // {
    // case 0x00: // No payload
    //     adamnet_ack();
    //     adamnet_special_00();
    //     break;
    // case 0x40: // Payload to Atari
    //     adamnet_ack();
    //     adamnet_special_40();
    //     break;
    // case 0x80: // Payload to Peripheral
    //     adamnet_ack();
    //     adamnet_special_80();
    //     break;
    // default:
    //     adamnet_nak();
    //     break;
    // }
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: ADAM call.
 */
void adamNetwork::adamnet_special_inquiry()
{
    // // Acknowledge
    // adamnet_ack();

    // Debug_printf("adamNetwork::adamnet_special_inquiry(%02x)\n", cmdFrame.aux1);

    // do_inquiry(cmdFrame.aux1);

    // // Finally, return the completed inq_dstats value back to Atari
    // bus_to_computer(&inq_dstats, sizeof(inq_dstats), false); // never errors.
}

void adamNetwork::do_inquiry(unsigned char inq_cmd)
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
 * and based on the return, signal adamnet_complete() or error().
 */
void adamNetwork::adamnet_special_00()
{
    // // Handle commands that exist outside of an open channel.
    // switch (cmdFrame.comnd)
    // {
    // case 'T':
    //     adamnet_set_translation();
    //     break;
    // case 'Z':
    //     adamnet_set_timer_rate();
    //     break;
    // default:
    //     if (protocol->special_00(&cmdFrame) == false)
    //         adamnet_complete();
    //     else
    //         adamnet_error();
    // }
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void adamNetwork::adamnet_special_40()
{
    // // Handle commands that exist outside of an open channel.
    // switch (cmdFrame.comnd)
    // {
    // case 0x30:
    //     adamnet_get_prefix();
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
void adamNetwork::adamnet_special_80()
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
    //     adamnet_do_idempotent_command_80();
    //     return;
    // case 0x2C: // CHDIR
    //     adamnet_set_prefix();
    //     return;
    // case 0xFD: // LOGIN
    //     adamnet_set_login();
    //     return;
    // case 0xFE: // PASSWORD
    //     adamnet_set_password();
    //     return;
    // }

    // memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // // Get special (devicespec) from computer
    // bus_to_peripheral(spData, SPECIAL_BUFFER_SIZE);

    // Debug_printf("adamNetwork::adamnet_special_80() - %s\n", spData);

    // // Do protocol action and return
    // if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
    //     adamnet_complete();
    // else
    //     adamnet_error();
}

void adamNetwork::adamnet_control_status()
{
    int64_t t = esp_timer_get_time() - AdamNet.start_time;

    if (t < 1500)
    {
        AdamNet.wait_for_idle();
        adamnet_response_status();
    }
}

void adamNetwork::adamnet_response_status()
{
    status_response[5] = status.byte;
    adamNetDevice::adamnet_response_status();
}

void adamNetwork::adamnet_control_send()
{

}

void adamNetwork::adamnet_control_clr()
{

}

void adamNetwork::adamnet_control_receive()
{
    
}

/**
 * Process incoming ADAM command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void adamNetwork::adamnet_process(uint8_t b)
{
    unsigned char c = b >> 4; // Seperate out command from node ID

    switch (c)
    {
    case MN_STATUS:
        adamnet_control_status();
        break;
    case MN_CLR:
        adamnet_control_clr();
        break;
    case MN_RECEIVE:
        adamnet_control_receive();
        break;
    case MN_SEND:
        adamnet_control_send();
        break;
    case MN_READY:
        adamnet_control_ready();
        break;
    }
}

/**
 * Check to see if PROCEED needs to be asserted, and assert if needed.
 */
void adamNetwork::adamnet_poll_interrupt()
{
    // if (protocol != nullptr)
    // {
    //     if (protocol->interruptEnable == false)
    //         return;

    //     protocol->fromInterrupt = true;
    //     protocol->status(&status);
    //     protocol->fromInterrupt = false;

    //     if (status.rxBytesWaiting > 0 || status.connected == 0)
    //         adamnet_assert_interrupt();

    //     reservedSave = status.connected;
    //     errorSave = status.error;
    // }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool adamNetwork::instantiate_protocol()
{
    // if (urlParser == nullptr)
    // {
    //     Debug_printf("adamNetwork::open_protocol() - urlParser is NULL. Aborting.\n");
    //     return false; // error.
    // }

    // // Convert to uppercase
    // transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

    // if (urlParser->scheme == "TCP")
    // {
    //     protocol = new NetworkProtocolTCP(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "UDP")
    // {
    //     protocol = new NetworkProtocolUDP(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "TEST")
    // {
    //     protocol = new NetworkProtocolTest(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "TELNET")
    // {
    //     protocol = new NetworkProtocolTELNET(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "TNFS")
    // {
    //     protocol = new NetworkProtocolTNFS(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "FTP")
    // {
    //     protocol = new NetworkProtocolFTP(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "HTTP" || urlParser->scheme == "HTTPS")
    // {
    //     protocol = new NetworkProtocolHTTP(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "SSH")
    // {
    //     protocol = new NetworkProtocolSSH(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else if (urlParser->scheme == "SMB")
    // {
    //     protocol = new NetworkProtocolSMB(receiveBuffer, transmitBuffer, specialBuffer);
    // }
    // else
    // {
    //     Debug_printf("Invalid protocol: %s\n", urlParser->scheme.c_str());
    //     return false; // invalid protocol.
    // }

    // if (protocol == nullptr)
    // {
    //     Debug_printf("adamNetwork::open_protocol() - Could not open protocol.\n");
    //     return false;
    // }

    // if (!login.empty())
    // {
    //     protocol->login = &login;
    //     protocol->password = &password;
    // }

    // Debug_printf("adamNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());
    // return true;
    return false;
}

void adamNetwork::parse_and_instantiate_protocol()
{
    // Clean up devicespec buffer.
    memset(devicespecBuf, 0, sizeof(devicespecBuf));

    // Get Devicespec from buffer, and put into primary devicespec string
    adamnet_recv_buffer(devicespecBuf, sizeof(devicespecBuf));
    util_clean_devicespec(devicespecBuf, sizeof(devicespecBuf));
    deviceSpec = string((char *)devicespecBuf);

    // Invalid URL returns error 165 in status.
    if (parseURL() == false)
    {
        Debug_printf("Invalid devicespec: %s\n", deviceSpec.c_str());
        status.byte = 0x00;
        status.bits.client_error = true;
        err = NETWORK_ERROR_INVALID_DEVICESPEC;
        adamnet_response_nack();
        return;
    }

    Debug_printf("Parse and instantiate protocol: %s\n", deviceSpec.c_str());

    // Instantiate protocol object.
    if (instantiate_protocol() == false)
    {
        Debug_printf("Could not open protocol.\n");
        status.byte = 0x00;
        status.bits.client_error = true;
        err = NETWORK_ERROR_GENERAL;
        adamnet_response_nack();
        return;
    }
}

/**
 * Start the Interrupt rate limiting timer
 */
void adamNetwork::timer_start()
{
    // esp_timer_create_args_t tcfg;
    // tcfg.arg = this;
    // tcfg.callback = onTimer;
    // tcfg.dispatch_method = esp_timer_dispatch_t::ESP_TIMER_TASK;
    // tcfg.name = nullptr;
    // esp_timer_create(&tcfg, &rateTimerHandle);
    // esp_timer_start_periodic(rateTimerHandle, timerRate * 1000);
}

/**
 * Stop the Interrupt rate limiting timer
 */
void adamNetwork::timer_stop()
{
    // // Delete existing timer
    // if (rateTimerHandle != nullptr)
    // {
    //     Debug_println("Deleting existing rateTimer\n");
    //     esp_timer_stop(rateTimerHandle);
    //     esp_timer_delete(rateTimerHandle);
    //     rateTimerHandle = nullptr;
    // }
}

/**
 * Is this a valid URL? (Used to generate ERROR 165)
 */
bool adamNetwork::isValidURL(EdUrlParser *url)
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
 * of adamNetwork.
 *
 * This function is a mess, because it has to be, maybe we can factor it out, later. -Thom
 */
bool adamNetwork::parseURL()
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

    Debug_printf("adamNetwork::parseURL(%s)\n", deviceSpec.c_str());

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
    urlParser = EdUrlParser::parseUrl(url);

    Debug_printf("adamNetwork::parseURL transformed to (%s, %s)\n", deviceSpec.c_str(), url.c_str());

    return isValidURL(urlParser);
}

/**
 * Called to pulse the PROCEED interrupt, rate limited by the interrupt timer.
 */
void adamNetwork::adamnet_assert_interrupt()
{
    // fnSystem.digital_write(PIN_PROC, interruptProceed == true ? DIGI_HIGH : DIGI_LOW);
}

void adamNetwork::adamnet_set_translation()
{
    // trans_aux2 = cmdFrame.aux2;
    // adamnet_complete();
}

void adamNetwork::adamnet_set_timer_rate()
{
    // timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // // Stop extant timer
    // timer_stop();

    // // Restart timer if we're running a protocol.
    // if (protocol != nullptr)
    //     timer_start();

    // adamnet_complete();
}

void adamNetwork::adamnet_do_idempotent_command_80()
{
    // adamnet_ack();

    // parse_and_instantiate_protocol();

    // if (protocol == nullptr)
    // {
    //     Debug_printf("Protocol = NULL\n");
    //     adamnet_error();
    //     return;
    // }

    // if (protocol->perform_idempotent_80(urlParser, &cmdFrame) == true)
    // {
    //     Debug_printf("perform_idempotent_80 failed\n");
    //     adamnet_error();
    // }
    // else
    //     adamnet_complete();
}

#endif /* BUILD_ADAM */