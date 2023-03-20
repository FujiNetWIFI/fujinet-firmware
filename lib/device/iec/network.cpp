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

    for (int i=0;i<16;i++)
    {
        receiveBuffer[i] = new string();
        transmitBuffer[i] = new string();
        specialBuffer[i] = new string();
    }
}

iecNetwork::~iecNetwork()
{
    Debug_printf("iwmNetwork::~iwmNetwork()\n");

    for (int i=0;i<16;i++)
    {
        delete receiveBuffer[i];
        delete transmitBuffer[i];
        delete specialBuffer[i];
    }
}

void iecNetwork::shutdown()
{
    // TODO: implement.
}

void iecNetwork::iec_open()
{
    mstr::toASCII(payload);
    deviceSpec[commanddata->channel].clear();
    deviceSpec[commanddata->channel].shrink_to_fit();

    // Prepend prefix, if available.
    if (!prefix[commanddata->channel].empty())
        deviceSpec[commanddata->channel] += prefix[commanddata->channel];

    deviceSpec[commanddata->channel] += payload;

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
    if (protocol[commanddata->channel] != nullptr)
    {
        protocol[commanddata->channel]->close();
        delete protocol[commanddata->channel];
        protocol[commanddata->channel] = nullptr;
    }

    urlParser[commanddata->channel] = EdUrlParser::parseUrl(deviceSpec[commanddata->channel]);

    // Convert scheme to uppercase
    std::transform(urlParser[commanddata->channel]->scheme.begin(), urlParser[commanddata->channel]->scheme.end(), urlParser[commanddata->channel]->scheme.begin(), ::toupper);

    // Instantiate based on scheme
    if (urlParser[commanddata->channel]->scheme == "TCP")
    {
        protocol[commanddata->channel] = new NetworkProtocolTCP(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "UDP")
    {
        protocol[commanddata->channel] = new NetworkProtocolUDP(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "TEST")
    {
        protocol[commanddata->channel] = new NetworkProtocolTest(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "TELNET")
    {
        protocol[commanddata->channel] = new NetworkProtocolTELNET(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "TNFS")
    {
        protocol[commanddata->channel] = new NetworkProtocolTNFS(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "FTP")
    {
        protocol[commanddata->channel] = new NetworkProtocolFTP(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "HTTP" || urlParser[commanddata->channel]->scheme == "HTTPS")
    {
        protocol[commanddata->channel] = new NetworkProtocolHTTP(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "SSH")
    {
        protocol[commanddata->channel] = new NetworkProtocolSSH(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else if (urlParser[commanddata->channel]->scheme == "SMB")
    {
        protocol[commanddata->channel] = new NetworkProtocolSMB(receiveBuffer[commanddata->channel], transmitBuffer[commanddata->channel], specialBuffer[commanddata->channel]);
    }
    else
    {
        Debug_printf("Invalid protocol: %s\n", urlParser[commanddata->channel]->scheme.c_str());
    }

    if (protocol[commanddata->channel] == nullptr)
    {
        Debug_printf("iwmNetwork::open_protocol() - Could not open protocol.\n");
    }

    if (!login.empty())
    {
        protocol[commanddata->channel]->login = &login;
        protocol[commanddata->channel]->password = &password;
    }

    Debug_printf("iwmNetwork::open_protocol() - Protocol %s opened.\n", urlParser[commanddata->channel]->scheme.c_str());

    // Attempt protocol open
    if (protocol[commanddata->channel]->open(urlParser[commanddata->channel], &cmdFrame) == true)
    {
        statusByte.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        delete protocol[commanddata->channel];
        protocol[commanddata->channel] = nullptr;
        return;
    }

    // Associate channel mode
    json[commanddata->channel] = new FNJSON();
    json[commanddata->channel]->setProtocol(protocol[commanddata->channel]);
}

void iecNetwork::iec_close()
{
    Debug_printf("iwmNetwork::close()\n");

    if (json[commanddata->channel] != nullptr)
    {
        delete json[commanddata->channel];
        json[commanddata->channel] = nullptr;
    }

    statusByte.byte = 0x00;

    // If no protocol enabled, we just signal complete, and return.
    if (protocol == nullptr)
    {
        return;
    }

    // Ask the protocol to close
    protocol[commanddata->channel]->close();

    // Delete the protocol object
    delete protocol[commanddata->channel];
    protocol[commanddata->channel] = nullptr;
}

void iecNetwork::iec_read()
{
    NetworkStatus ns;

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        Debug_printf("iec_read() - Not connected\n");
        IEC.senderTimeout();
        return;
    }

    // Get status
    protocol[commanddata->channel]->status(&ns);

    if (!ns.rxBytesWaiting)
    {
        IEC.senderTimeout();
        return;
    }

    while (ns.rxBytesWaiting > 1)
    {
        protocol[commanddata->channel]->read(1);
        IEC.sendByte(receiveBuffer[commanddata->channel]->at(0), false);
        receiveBuffer[commanddata->channel]->erase(0, 1);
    }

    IEC.sendByte(receiveBuffer[commanddata->channel]->at(0), true);
    receiveBuffer[commanddata->channel]->erase(0, 1);
}

void iecNetwork::iec_write()
{
    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        Debug_printf("iec_open_save() - Not connected\n");
        return;
    }

    Debug_printf("Receiving data from computer...\n");

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        if (b < 0)
        {
            Debug_printf("error on receive.\n");
            return;
        }

        transmitBuffer[commanddata->channel]->push_back(b);
    }

    Debug_printf("Received %u bytes. Transmitting.\n", transmitBuffer[commanddata->channel]->length());

    protocol[commanddata->channel]->write(transmitBuffer[commanddata->channel]->length());
    transmitBuffer[commanddata->channel]->clear();
    transmitBuffer[commanddata->channel]->shrink_to_fit();
}

void iecNetwork::iec_reopen_channel()
{
    switch (device_state)
    {
    case DEVICE_LISTEN:
        iec_write();
        break;
    case DEVICE_TALK:
        iec_read();
        break;
    default:
        break;
    }
}

void iecNetwork::iec_reopen_save()
{
    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        Debug_printf("iec_open_save() - Not connected\n");
        return;
    }

    Debug_printf("Receiving data from computer...\n");

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        if (b < 0)
        {
            Debug_printf("error on receive.\n");
            return;
        }

        transmitBuffer[commanddata->channel]->push_back(b);
    }

    Debug_printf("Received %u bytes. Transmitting.\n", transmitBuffer[commanddata->channel]->length());

    protocol[commanddata->channel]->write(transmitBuffer[commanddata->channel]->length());
    transmitBuffer[commanddata->channel]->clear();
    transmitBuffer[commanddata->channel]->shrink_to_fit();
}

void iecNetwork::iec_reopen_load()
{
    NetworkStatus ns;
    bool eoi = false;

    if ((protocol[commanddata->channel] == nullptr) || (receiveBuffer[commanddata->channel] == nullptr))
        return; // Punch out.

    // Get status
    protocol[commanddata->channel]->status(&ns);

    if (!ns.rxBytesWaiting)
    {
        IEC.senderTimeout();
        return;
    }

    while (!eoi)
    {
        // Truncate bytes waiting to response size
        ns.rxBytesWaiting = (ns.rxBytesWaiting > 65534) ? 65534 : ns.rxBytesWaiting;

        Debug_printf("bytes waiting: %u connected: %u error %u \n", ns.rxBytesWaiting, ns.connected, ns.error);

        int blockSize = ns.rxBytesWaiting;

        Debug_printf("Reading %u bytes from stream\n", blockSize);

        if (protocol[commanddata->channel]->read(blockSize)) // protocol adapter returned error
        {
            Debug_printf("WE GOT YOINKED\n");
            IEC.senderTimeout();
            statusByte.bits.client_error = true;
            err = protocol[commanddata->channel]->error;
            return;
        }

        // Do another status
        protocol[commanddata->channel]->status(&ns);

        if ((!ns.connected) || ns.error == 136) // EOF
            eoi = true;

        // Now send the resulting block of data through the bus
        for (int i = 0; i < blockSize; i++)
        {
            int lastbyte = blockSize - 2;
            if ((i == lastbyte) && (eoi == true))
            {
                IEC.sendByte(receiveBuffer[commanddata->channel]->at(i), true);
                break;
            }
            else
                IEC.sendByte(receiveBuffer[commanddata->channel]->at(i), false);
        }

        receiveBuffer[commanddata->channel]->erase(0, blockSize);
    }
}

void iecNetwork::set_prefix()
{
    uint8_t prefixSpec[256];
    vector<string> t = util_tokenize(payload, ',');
    string prefixSpec_str;
    int channel = 0;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    if (t.size() < 2)
    {
        Debug_printf("Channel # required\n");
        response_queue.push("error: channel # required\r");
        return;
    }
    else if (t.size() == 2) // clear prefix
    {
        Debug_printf("Prefix cleared\n");
        channel = atoi(t[1].c_str());
        prefix[channel].clear();
        prefix[channel].shrink_to_fit();
        response_queue.push("prefix cleared\r");
        return;
    }
    else
    {
        channel = atoi(t[1].c_str());
        strncpy((char *)prefixSpec, t[2].c_str(), 256);
    }

    util_clean_devicespec(prefixSpec, sizeof(prefixSpec));

    prefixSpec_str = string((const char *)prefixSpec);
    Debug_printf("iecNetwork::sio_set_prefix(%s)\n", prefixSpec_str.c_str());

    if (prefixSpec_str == "..") // Devance path N:..
    {
        vector<int> pathLocations;
        for (int i = 0; i < prefix[channel].size(); i++)
        {
            if (prefix[channel][i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (prefix[channel][prefix[channel].size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        prefix[channel] = prefix[channel].substr(0, pathLocations.back() + 1);
    }
    else if (prefixSpec_str == "/") // Go back to hostname.
    {
        // TNFS://foo.com/path
        size_t pos = prefix[channel].find("/");

        if (pos == string::npos)
        {
            prefix[channel].clear();
            prefix[channel].shrink_to_fit();
        }

        pos = prefix[channel].find("/", ++pos);

        if (pos == string::npos)
        {
            prefix[channel].clear();
            prefix[channel].shrink_to_fit();
        }

        pos = prefix[channel].find("/", ++pos);

        if (pos == string::npos)
            prefix[channel] += "/";

        pos = prefix[channel].find("/", ++pos);

        prefix[channel] = prefix[channel].substr(0, pos);
    }
    else if (prefixSpec_str[0] == '/') // N:/DIR
    {
        prefix[channel] = prefixSpec_str;
    }
    else if (prefixSpec_str.empty())
    {
        prefix[channel].clear();
    }
    else if (prefixSpec_str.find_first_of(":") != string::npos)
    {
        prefix[channel] = prefixSpec_str;
    }
    else // append to path.
    {
        prefix[channel] += prefixSpec_str;
    }

    prefix[channel] = util_get_canonical_path(prefix[channel]);

    Debug_printf("Prefix now: %s\n", prefix[channel].c_str());

    // We are okay, signal complete.
    response_queue.push("ok\r");
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
        iec_open();
        break;
    case IEC_CLOSE:
        iec_close();
        break;
    case IEC_REOPEN: // Data
        iec_reopen_save();
        break;
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

void iecNetwork::data_waiting()
{
    vector<string> t = util_tokenize(payload,',');
    string data_waiting_yes = "1\r";
    string data_waiting_no = "0\r";
    NetworkStatus ns;

    if (t.size()<2)
    {
        Debug_printf("No channel #, sending 0\n");
        response_queue.push(data_waiting_no);
        return;
    }

    int channel = atoi(t[1].c_str());

    Debug_printf("Channel: %u\n",channel);

    if (protocol[channel]==nullptr)
    {
        Debug_printf("No protocol for channel #%u, sending 0\n",channel);
        response_queue.push(data_waiting_no);
        return;
    }

    protocol[channel]->status(&ns);

    if (ns.rxBytesWaiting)
        response_queue.push(data_waiting_yes);
    else
        response_queue.push(data_waiting_no);
}

void iecNetwork::process_command()
{
    Debug_printf("process_command()\n");

    if (payload.find("PREFIX") != string::npos)
        set_prefix();
    else if (payload.find("!") != string::npos)
        data_waiting();
}

void iecNetwork::process_channel()
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
        iec_reopen_channel();
        break;
    default:
        Debug_printf("Uncaught command.\n");
        break;
    }
}

device_state_t iecNetwork::process(IECData *id)
{
    // Call base class
    virtualDevice::process(id);

    // only process command channel on unlisten
    // if (commanddata->channel == 15)
    //     if (commanddata->primary != 0x3F)
    //         return device_state;

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