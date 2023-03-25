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

    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        protocol[i] = nullptr;
        receiveBuffer[i] = new string();
        transmitBuffer[i] = new string();
        specialBuffer[i] = new string();
    }

    iecStatus.channel = 15;
    iecStatus.connected = 0;
    iecStatus.msg = "fujinet network device";
    iecStatus.bw = 0;
}

iecNetwork::~iecNetwork()
{
    for (int i = 0; i < NUM_CHANNELS; i++)
    {
        delete receiveBuffer[i];
        delete transmitBuffer[i];
        delete specialBuffer[i];
    }
}

void iecNetwork::iec_open()
{
    file_not_found = false;
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
        file_not_found = true;
        return;
    }

    if (protocol[commanddata->channel] == nullptr)
    {
        Debug_printf("iwmNetwork::open_protocol() - Could not open protocol.\n");
        file_not_found = true;
        return;
    }

    if (!login[commanddata->channel].empty())
    {
        protocol[commanddata->channel]->login = &login[commanddata->channel];
        protocol[commanddata->channel]->password = &password[commanddata->channel];
    }

    Debug_printf("iwmNetwork::open_protocol() - Protocol %s opened.\n", urlParser[commanddata->channel]->scheme.c_str());

    // Attempt protocol open
    if (protocol[commanddata->channel]->open(urlParser[commanddata->channel], &cmdFrame) == true)
    {
        Debug_printf("Protocol unable to make connection.\n");
        delete protocol[commanddata->channel];
        protocol[commanddata->channel] = nullptr;
        return;
    }

    // Associate channel mode
    json[commanddata->channel] = new FNJSON();
    json[commanddata->channel]->setProtocol(protocol[commanddata->channel]);

    if (file_not_found)
    {
        iecStatus.channel = commanddata->channel;
        iecStatus.bw = 0;
        iecStatus.connected = false;
        iecStatus.msg = "not found";
    }
    else
    {
        NetworkStatus ns;

        protocol[commanddata->channel]->status(&ns);
        iecStatus.channel = commanddata->channel;
        iecStatus.bw = ns.rxBytesWaiting;
        iecStatus.connected = true;
        iecStatus.msg = "opened";
    }
}

void iecNetwork::iec_close()
{
    Debug_printf("iecNetwork::close()\n");

    iecStatus.channel = commanddata->channel;
    iecStatus.bw = 0;
    iecStatus.connected = 0;
    iecStatus.msg = "closed";

    if (json[commanddata->channel] != nullptr)
    {
        delete json[commanddata->channel];
        json[commanddata->channel] = nullptr;
    }

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

void iecNetwork::iec_reopen_load()
{
    NetworkStatus ns;
    bool eoi = false;

    if ((protocol[commanddata->channel] == nullptr) || (receiveBuffer[commanddata->channel] == nullptr))
    {
        IEC.senderTimeout();
        return; // Punch out.
    }

    if (file_not_found)
    {
        IEC.senderTimeout();
        return;
    }

    // Get status
    protocol[commanddata->channel]->status(&ns);

    if (!ns.rxBytesWaiting)
    {
        Debug_printf("What happened?\n");
        IEC.senderTimeout();

        iecStatus.bw = ns.rxBytesWaiting;
        iecStatus.msg = "no bytes waiting";
        iecStatus.connected = ns.connected;
        iecStatus.channel = commanddata->channel;
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
            iecStatus.bw = ns.rxBytesWaiting;
            iecStatus.msg = "read error";
            iecStatus.connected = ns.connected;
            iecStatus.channel = commanddata->channel;

            IEC.senderTimeout();
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
                // We're done.
                IEC.sendByte(receiveBuffer[commanddata->channel]->at(i), true);
                break;
            }
            else
            {
                IEC.sendByte(receiveBuffer[commanddata->channel]->at(i), false);
            }
        }

        receiveBuffer[commanddata->channel]->erase(0, blockSize);
    }

    iecStatus.bw = ns.rxBytesWaiting;
    iecStatus.msg = "eof";
    iecStatus.connected = ns.connected;
    iecStatus.channel = commanddata->channel;
}

void iecNetwork::iec_reopen_save()
{
    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        iecStatus.bw = 0;
        iecStatus.channel = commanddata->channel;
        iecStatus.msg = "not connected";
        iecStatus.connected = 0;

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

    if (protocol[commanddata->channel]->write(transmitBuffer[commanddata->channel]->length()))
    {
        iecStatus.bw = 0;
        iecStatus.msg = "write error";
        iecStatus.connected = 0;
        iecStatus.channel = commanddata->channel;
    }

    transmitBuffer[commanddata->channel]->clear();
    transmitBuffer[commanddata->channel]->shrink_to_fit();
}

void iecNetwork::set_login_password()
{
    int channel = 0;

    if (pt.size() == 1)
    {
        iecStatus.bw=0;
        iecStatus.msg = "usage login,chan,username,password";
        iecStatus.connected = 0;
        iecStatus.channel = commanddata->channel;
        return;
    }
    else if (pt.size() == 2)
    {
        // Clear login for channel X
        char reply[40];

        channel = atoi(pt[1].c_str());

        login[channel].clear();
        login[channel].shrink_to_fit();

        snprintf(reply,40,"login cleared for channel %u",channel);

        iecStatus.bw=0;
        iecStatus.msg = string(reply);
        iecStatus.connected = 0;
        iecStatus.channel = commanddata->channel;
    }
    else
    {
        char reply[40];

        channel = atoi(pt[1].c_str());

        login[channel] = pt[2];
        password[channel] = pt[3];

        snprintf(reply,40,"login set for channel %u",channel);

        iecStatus.bw=0;
        iecStatus.msg = string(reply);
        iecStatus.connected = 0;
        iecStatus.channel = commanddata->channel;
    }
}

void iecNetwork::iec_listen_command()
{
}

void iecNetwork::iec_talk_command()
{
    if (response_queue.empty())
        iec_talk_command_buffer_status();
}

void iecNetwork::iec_talk_command_buffer_status()
{
    char reply[80];

    snprintf(reply,80,"%u,\"%s\",%u,%u",iecStatus.bw,iecStatus.msg.c_str(),iecStatus.connected,iecStatus.channel);
    IEC.sendBytes(string(reply));
}

void iecNetwork::iec_command()
{
    if (pt[0] == "cd")
        set_prefix();
    else if (pt[0] == "pwd")
        get_prefix();
    else if (pt[0] == "login")
        set_login_password();
}

void iecNetwork::get_prefix()
{
    int channel=-1;

    if (pt.size()<2)
    {
        iecStatus.bw=0;
        iecStatus.connected=0;
        iecStatus.channel=channel;
        iecStatus.msg="need channel #";
        return;
    }

    channel = atoi(pt[1].c_str());

    iecStatus.bw=0;
    iecStatus.msg=prefix[channel];
    iecStatus.connected=0;
    iecStatus.channel=channel;
}

void iecNetwork::set_prefix()
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;
    int channel = -1;

    Debug_printf("set_prefix(%s)", payload.c_str());

    memset(prefixSpec, 0, sizeof(prefixSpec));

    if (pt.size() < 2)
    {
        Debug_printf("Channel # required\n");
        iecStatus.bw = 0;
        iecStatus.msg = "channel # required";
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        return;
    }
    else if (pt.size() == 2) // clear prefix
    {
        Debug_printf("Prefix cleared\n");
        channel = atoi(pt[1].c_str());
        prefix[channel].clear();
        prefix[channel].shrink_to_fit();
        iecStatus.bw = 0;
        iecStatus.msg = "prefix cleared";
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        return;
    }
    else
    {
        channel = atoi(pt[1].c_str());
        strncpy((char *)prefixSpec, pt[2].c_str(), 256);
    }

    util_clean_devicespec(prefixSpec, sizeof(prefixSpec));

    prefixSpec_str = string((const char *)prefixSpec);
    Debug_printf("iecNetwork::set_prefix(%s)\n", prefixSpec_str.c_str());

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

    iecStatus.bw = 0;
    iecStatus.msg = prefix[channel];
    iecStatus.connected = 0;
    iecStatus.channel = channel;

    Debug_printf("Prefix now: %s\n", prefix[channel].c_str());
}

device_state_t iecNetwork::process(IECData *_commanddata)
{
    // Call base class
    virtualDevice::process(_commanddata); // commanddata set here.

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
    case IEC_REOPEN:
        iec_reopen_load();
        break;
    default:
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
    case IEC_REOPEN:
        iec_reopen_save();
        break;
    default:
        break;
    }
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
    case IEC_REOPEN:
        break;
    default:
        break;
    }
}

void iecNetwork::process_command()
{
    if (commanddata->primary == IEC_TALK && commanddata->secondary == IEC_REOPEN)
    {
        iec_talk_command();
        return;
    }
    else if (commanddata->primary == IEC_LISTEN && commanddata->secondary == IEC_REOPEN)
        iec_command();
}

#endif /* BUILD_IEC */