#ifdef BUILD_IEC
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

iecNetwork::iecNetwork()
{
    Debug_printf("iwmNetwork::iwmNetwork()\n");
    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
}

iecNetwork::~iecNetwork()
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

void iecNetwork::shutdown()
{
    // TODO: implement.
}

void iecNetwork::iec_open()
{
    mstr::toASCII(payload);
    deviceSpec = payload;

    channelMode[commanddata->channel] = PROTOCOL;

    switch (commanddata->channel)
    {
    case 0:                // load
        cmdFrame.aux1 = 4; // read
        cmdFrame.aux2 = 0; // no translation
        break;
    case 1:                // save
        cmdFrame.aux1 = 8; // write
        cmdFrame.aux2 = 0; // no translation
        break;
    default:
        cmdFrame.aux1 = 12; // default read/write
        cmdFrame.aux2 = translationMode[commanddata->channel];
        break;
    }

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        delete protocol;
        protocol = nullptr;
    }

    urlParser = EdUrlParser::parseUrl(deviceSpec);

    // Convert scheme to uppercase
    std::transform(urlParser->scheme.begin(), urlParser->scheme.end(), urlParser->scheme.begin(), ::toupper);

    // Instantiate based on scheme
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
    }

    if (protocol == nullptr)
    {
        Debug_printf("iwmNetwork::open_protocol() - Could not open protocol.\n");
    }

    if (!login.empty())
    {
        protocol->login = &login;
        protocol->password = &password;
    }

    Debug_printf("iwmNetwork::open_protocol() - Protocol %s opened.\n", urlParser->scheme.c_str());

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

void iecNetwork::iec_close()
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

void iecNetwork::iec_reopen_load()
{
    NetworkStatus ns;
    char *data_buffer;
    bool eoi = false;

    data_buffer = (char *)malloc(65535);

    if (!data_buffer)
        return; // punch out

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    // Get status
    protocol->status(&ns);

    while (!eoi)
    {
        // Truncate bytes waiting to response size
        ns.rxBytesWaiting = (ns.rxBytesWaiting > 16384) ? 16384 : ns.rxBytesWaiting;

        int blockSize = ns.rxBytesWaiting;

        Debug_printf("Reading %u bytes from stream\n",blockSize);

        if (protocol->read(blockSize)) // protocol adapter returned error
        {
            statusByte.bits.client_error = true;
            err = protocol->error;
            return;
        }
        else // everything ok
        {
            memcpy(data_buffer, receiveBuffer->data(), blockSize);
            receiveBuffer->erase(0, blockSize);
        }

        // Do another status
        protocol->status(&ns);

        if (!ns.connected) // EOF
            eoi = true;

        // Now send the resulting block of data through the bus
        for (int i = 0; i < blockSize; i++)
        {
            int lastbyte = blockSize-2;
            if ((i == lastbyte) && (eoi == true))
            {
                IEC.sendByte(data_buffer[i],true);
                break;
            }
            else
                IEC.sendByte(data_buffer[i],false);
        }
    }

    free(data_buffer);
}

void iecNetwork::process_load()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN: // Data
        iec_reopen_load();
        break;
    default:
        Debug_printf("Uncaught LOAD command. %u\n", commanddata->secondary);
        break;
    }
}

void iecNetwork::process_save()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
    case IEC_CLOSE:
    case IEC_REOPEN: // Data
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

void iecNetwork::process_command()
{
    // Add commands
}

void iecNetwork::process_channel()
{
    switch (commanddata->secondary)
    {
    case IEC_OPEN:
    case IEC_CLOSE:
    case IEC_REOPEN: // Data
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

device_state_t iecNetwork::process(IECData *id)
{
    // Call base class
    virtualDevice::process(id);

    // fan out to appropriate process routine
    switch (commanddata->channel)
    {
    case 0:
        process_load();
        break;
    case 1:
        process_save();
        break;
    case 15:
        process_command();
        break;
    default:
        process_channel();
        break;
    }

    return device_state;
}

#endif