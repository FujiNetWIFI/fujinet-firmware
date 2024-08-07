#ifdef BUILD_IEC
/**
 * N: Firmware
 */

#include <algorithm>
#include <cstring>
#include <cstdint>

#include "network.h"

#include "../../include/debug.h"
#include "../../hardware/led.h"

#include "utils.h"

#include "status_error_codes.h"
#include "NetworkProtocolFactory.h"
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
    iecStatus.channel = CHANNEL_COMMAND;
    iecStatus.connected = 0;
    iecStatus.msg = "fujinet network device";
    iecStatus.error = NETWORK_ERROR_SUCCESS;
}

iecNetwork::~iecNetwork()
{
}

void iecNetwork::poll_interrupt(uint8_t c)
{
    NetworkStatus ns;
    auto& protocol = network_data_map[c].protocol;
    if (protocol)
    {
        if (!protocol->interruptEnable)
            return;

        protocol->fromInterrupt = true;
        protocol->status(&ns);
        protocol->fromInterrupt = false;

        if (ns.rxBytesWaiting > 0 || ns.connected == 0)
            IEC.assert_interrupt();
    }
}

void iecNetwork::iec_open()
{
    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId]; // This will create the channel if it doesn't exist

    uint8_t channel_aux1 = 12;
    uint8_t channel_aux2 = channel_data.translationMode; // not sure about this, you can't set this unless you send a command for the channel first, I think it relies on the array being init to 0s

    Debug_printv("commanddata: prim:%02x, dev:%02x, 2nd:%02x, chan:%02x\r\n", commanddata.primary, commanddata.device, commanddata.secondary, commanddata.channel);

    file_not_found = false;


    channel_data.deviceSpec.clear();
    if (!channel_data.prefix.empty()) {
        channel_data.deviceSpec += channel_data.prefix;
    }

        // Check if the payload is RAW (i.e. from fujinet-lib) by the presence of "01" as the first byte, which can't happen for BASIC.
    // If it is, then the next 2 bytes are the aux1/aux2 values (mode and trans), and the rest is the actual URL.
    // This is an efficiency so we don't have to send a 2nd command to tell it what the parameters should have been. BASIC will still need to use "openparams" command, as the OPEN line doesn't have capacity for the parameters (can't use a "," as that's a valid URL character)
    if (payload[0] == 0x01) {
        channel_aux1 = payload[1];
        channel_aux2 = payload[2];

        // capture the trans mode as though "settrans" had been invoked
        channel_data.translationMode = channel_aux2;
        // translationMode[commanddata.channel] = channel_aux2;
        
        // remove the marker bytes so the payload can continue as with BASIC setup
        payload = payload.substr(3);
    }

    if (payload != "$") {
        clean_transform_petscii_to_ascii(payload);
        Debug_printv("transformed payload to %s\r\n", payload.c_str());
        channel_data.deviceSpec += payload;
    }

    channel_data.channelMode = NetworkData::PROTOCOL;

    cmdFrame.aux1 = (channelId == CHANNEL_LOAD) ? 4 : (channelId == CHANNEL_SAVE) ? 8 : channel_aux1;
    cmdFrame.aux2 = (channelId == CHANNEL_LOAD || channelId == CHANNEL_SAVE) ? 0 : channel_aux2;

    // Reset protocol if it exists
    channel_data.protocol.reset();
    channel_data.urlParser = std::move(PeoplesUrlParser::parseURL(channel_data.deviceSpec));

    // Convert scheme to uppercase
    std::transform(channel_data.urlParser->scheme.begin(), channel_data.urlParser->scheme.end(), channel_data.urlParser->scheme.begin(), 
                   [](unsigned char c) { return std::toupper(c); });

    // Instantiate protocol based on the scheme
    Debug_printv("Creating protocol for chema %s\r\n", channel_data.urlParser->scheme.c_str());
    channel_data.protocol = std::move(NetworkProtocolFactory::createProtocol(channel_data.urlParser->scheme, channel_data));

    if (!channel_data.protocol) {
        Debug_printf("Invalid protocol: %s\r\n", channel_data.urlParser->scheme.c_str());
        file_not_found = true; // Assuming file_not_found is accessible here
        return;
    }

    // Set login and password if they exist
    if (!channel_data.login.empty()) {
        // TODO: Change the NetworkProtocol password and login to STRINGS FFS
        channel_data.protocol->login = &channel_data.login;
        channel_data.protocol->password = &channel_data.password;
    }

    Debug_printv("Protocol %s opened.\r\n", channel_data.urlParser->scheme.c_str());

    if (channel_data.protocol->open(channel_data.urlParser.get(), &cmdFrame)) {
        Debug_printv("Protocol unable to make connection.\r\n");
        channel_data.protocol.reset(); // Clean up the protocol
        file_not_found = true;
        return;
    }

    // assert SRQ
    IEC.assert_interrupt();

    channel_data.json = std::make_unique<FNJSON>();
    channel_data.json->setProtocol(channel_data.protocol.get());

}

void iecNetwork::iec_close()
{
    Debug_printf("iecNetwork::iec_close()\r\n");

    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    iecStatus.channel = commanddata.channel;
    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.connected = 0;
    iecStatus.msg = "closed";

    channel_data.json.reset();

    // If no protocol enabled, we just signal complete, and return.
    if (channel_data.protocol == nullptr) return;

    // Close protocol and clean up
    channel_data.protocol->close();
    channel_data.protocol = nullptr;
    channel_data.receiveBuffer.clear();
    channel_data.transmitBuffer.clear();
    channel_data.specialBuffer.clear();

    commanddata.init();
    state = DEVICE_IDLE;
    Debug_printv("device init");
}

void iecNetwork::iec_reopen_load()
{
    NetworkStatus ns;
    bool eoi = false;

    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    if (!channel_data.protocol) {
        Debug_printv("No protocol set\r\n");
        return;
    }

    if (file_not_found)
    {
        Debug_printv("file not found");
        IEC.senderTimeout();
        return;
    }

    // Get status
    channel_data.protocol->status(&ns);

    if (!ns.rxBytesWaiting)
    {
        Debug_printv("What happened?\r\n");
        IEC.senderTimeout();

        iecStatus.error = NETWORK_ERROR_GENERAL_TIMEOUT;
        iecStatus.msg = "no bytes waiting";
        iecStatus.connected = ns.connected;
        iecStatus.channel = commanddata.channel;
        return;
    }

    while (!eoi)
    {
        // Truncate bytes waiting to response size
        ns.rxBytesWaiting = (ns.rxBytesWaiting > 65534) ? 65534 : ns.rxBytesWaiting;

        Debug_printf("bytes waiting: %u connected: %u error %u \r\n", ns.rxBytesWaiting, ns.connected, ns.error);

        int blockSize = ns.rxBytesWaiting;

        Debug_printf("Reading %u bytes from stream\r\n", blockSize);

        if (channel_data.protocol->read(blockSize)) // protocol adapter returned error
        {
            iecStatus.error = NETWORK_ERROR_GENERAL;
            iecStatus.msg = "read error";
            iecStatus.connected = ns.connected;
            iecStatus.channel = channelId;
            Debug_printv("Read Error");
            IEC.senderTimeout();
            return;
        }

        // Do another status
        channel_data.protocol->status(&ns);

        if ((!ns.connected) || ns.error == 136) // EOF
            eoi = true;

        IEC.sendBytes(channel_data.receiveBuffer, true);
        channel_data.receiveBuffer.erase(0, blockSize);
    }

    iecStatus.error = NETWORK_ERROR_END_OF_FILE;
    iecStatus.msg = "eof";
    iecStatus.connected = ns.connected;
    iecStatus.channel = channelId;
}

void iecNetwork::iec_reopen_save()
{
    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    // If protocol isn't connected, then return not connected.
    if (!channel_data.protocol)
    {
        iecStatus.error = NETWORK_ERROR_NOT_CONNECTED;
        iecStatus.channel = channelId;
        iecStatus.msg = "not connected";
        iecStatus.connected = 0;

        Debug_printf("iec_reopen_save() - Not connected\r\n");
        return;
    }

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        if (b < 0)
        {
            Debug_printf("error on receive.\r\n");
            return;
        }

        channel_data.transmitBuffer.push_back(b);
    }

    // force incoming data from HOST to fixed ascii
    // Debug_printv("[1] DATA: >%s< [%s]", channel_data.transmitBuffer.c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());
    clean_transform_petscii_to_ascii(channel_data.transmitBuffer);
    // Debug_printv("[2] DATA: >%s< [%s]", transmitBuffer[commanddata.channel]->c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());

    Debug_printf("Received %u bytes. Transmitting.\r\n", channel_data.transmitBuffer.length());

    if (channel_data.protocol->write(channel_data.transmitBuffer.length()))
    {
        iecStatus.error = NETWORK_ERROR_GENERAL;
        iecStatus.msg = "write error";
        iecStatus.connected = 0;
        iecStatus.channel = channelId;
    }

    channel_data.transmitBuffer.clear();
}

void iecNetwork::iec_reopen_channel()
{
    Debug_printv("primary[%2X]", commanddata.primary);
    switch (commanddata.primary)
    {
    case IEC_TALK:
        iec_reopen_channel_talk();
        break;
    case IEC_LISTEN:
        iec_reopen_channel_listen();
        break;
    }
}

void iecNetwork::iec_reopen_channel_listen()
{
    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    Debug_printv("channel[%2X]", channelId);

    if (!channel_data.protocol)
    {
        Debug_printf("iec_reopen_channel_listen() - Not connected\r\n");
        IEC.senderTimeout();
        return;
    }

    // Debug_printv("Receiving data from computer...\r\n");

    while (!(IEC.flags & EOI_RECVD))
    {
        int16_t b = IEC.receiveByte();

        if (b < 0)
        {
            Debug_printf("error on receive.\r\n");
            return;
        }

        channel_data.transmitBuffer.push_back(b);
    }

    // force incoming data from HOST to fixed ascii
    // Debug_printv("[1] DATA: >%s< [%s]", channel_data.transmitBuffer.c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());
    clean_transform_petscii_to_ascii(channel_data.transmitBuffer);
    // Debug_printv("[2] DATA: >%s< [%s]", channel_data.transmitBuffer.c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());

    Debug_printf("Received %u bytes. Transmitting.\r\n", channel_data.transmitBuffer.length());

    channel_data.protocol->write(channel_data.transmitBuffer.length());
    channel_data.transmitBuffer.clear();
    channel_data.transmitBuffer.shrink_to_fit();
}

void iecNetwork::iec_reopen_channel_talk()
{
    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    bool set_eoi = false;
    NetworkStatus ns;

    Debug_printv("channel[%2X]", channelId);

    // If protocol isn't connected, then return not connected.
    if (!channel_data.protocol)
    {
        Debug_printf("iec_reopen_channel_talk() - Not connected\r\n");
        return;
    }

    if (channel_data.receiveBuffer.empty())
    {
        channel_data.protocol->status(&ns);

        if (ns.rxBytesWaiting)
            channel_data.protocol->read(ns.rxBytesWaiting);
    }

    if (channel_data.receiveBuffer.empty())
    {
        Debug_printv("Receive Buffer Empty.");
        IEC.senderTimeout();
        return;
    }

    // ALWAYS translate the data to PETSCII towards the host. Translation mode needs rewriting.
    util_devicespec_fix_9b((uint8_t *) channel_data.receiveBuffer.data(), channel_data.receiveBuffer.length());
    channel_data.receiveBuffer = mstr::toPETSCII2(channel_data.receiveBuffer);

    // Debug_printv("TALK: sending data to host: >%s< [%s]", receiveBuffer[commanddata.channel]->c_str(), mstr::toHex(*receiveBuffer[commanddata.channel]).c_str());
    do
    {
        char b = channel_data.receiveBuffer.front();

        if (channel_data.receiveBuffer.empty())
        {
            //Debug_printv("Receive Buffer Empty.");
            set_eoi = true;
        }

        IEC.sendByte(b, set_eoi);

        if ( IEC.flags & ERROR )
        {
            Debug_printv("TALK ERROR! flags[%d]\n", IEC.flags);
            return;
        }

        if ( !(IEC.flags & ATN_PULLED) )
            channel_data.receiveBuffer.erase(0, 1);

    } while( !(IEC.flags & ATN_PULLED) && !set_eoi );
}

void iecNetwork::set_login_password()
{
    int channel = 0;

    if (pt.size() == 1)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "usage login,chan,username,password";
        iecStatus.connected = 0;
        iecStatus.channel = commanddata.channel;
        return;
    }
    else if (pt.size() == 2)
    {
        // Clear login for channel X
        char reply[40];

        channel = atoi(pt[1].c_str());
        auto& channel_data = network_data_map[channel];

        channel_data.login.clear();
        channel_data.login.shrink_to_fit();

        snprintf(reply, 40, "login cleared for channel %u", channel);

        iecStatus.error = NETWORK_ERROR_SUCCESS;
        iecStatus.msg = string(reply);
        iecStatus.connected = 0;
        iecStatus.channel = channel;
    }
    else
    {
        char reply[40];

        channel = atoi(pt[1].c_str());
        auto& channel_data = network_data_map[channel];

        channel_data.login = pt[2];
        channel_data.password = pt[3];

        snprintf(reply, 40, "login set for channel %u", channel);

        iecStatus.error = NETWORK_ERROR_SUCCESS;
        iecStatus.msg = string(reply);
        iecStatus.connected = 0;
        iecStatus.channel = channel;
    }
}

void iecNetwork::parse_json()
{
    int channel;
    NetworkStatus ns;

    if (pt.size() < 2)
    {
        Debug_printf("parse_json - no channel specified\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "no channel specified";
        iecStatus.channel = 0;
        iecStatus.connected = 0;
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    channel_data.protocol->status(&ns);

    if (!channel_data.json->parse())
    {
        Debug_printf("could not parse json\r\n");
        iecStatus.error = NETWORK_ERROR_GENERAL;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "could not parse json";
    }
    else
    {
        Debug_printf("json parsed\r\n");
        iecStatus.error = NETWORK_ERROR_SUCCESS;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "json parsed";
    }
}

void iecNetwork::query_json()
{
    int channel = 0;
    char reply[80];
    string s;

    Debug_printf("query_json(%s)\r\n", payload.c_str());

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "invalid # of parameters";
        iecStatus.channel = 0;
        iecStatus.connected = 0;
        Debug_printf("Invalid # of parameters to set_json_query()\r\n");
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    s = pt.size() == 2 ? "" : pt[2];  // allow empty string if there aren't enough args

    Debug_printf("Channel: %u\r\n", channel);

    channel_data.json->setReadQuery(s, 0);

    if (!channel_data.json->readValueLen())
    {
        iecStatus.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        iecStatus.channel = channel;
        iecStatus.connected = 0;
        iecStatus.msg = "query not found";
        return;
    }

    size_t len = channel_data.json->readValueLen();
    std::vector<uint8_t> buffer(len);
    channel_data.json->readValue(buffer.data(), buffer.size());
    channel_data.receiveBuffer += std::string(buffer.begin(), buffer.end());

    snprintf(reply, 80, "query set to %s", s.c_str());
    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.channel = channel;
    iecStatus.connected = true;
    iecStatus.msg = string(reply);
    Debug_printf("Query set to %s\r\n", s.c_str());
}

void iecNetwork::parse_bite()
{
    int channel = 0;
    int bite_size = 79;
    NetworkStatus ns;

    if (pt.size() < 2)
    {
        Debug_printf("parse_bite - no channel specified\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "no channel specified";
        iecStatus.channel = 0;
        iecStatus.connected = 0;
        return;
    }
    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    if (pt.size() == 3)
    {
        bite_size = atoi(pt[2].c_str()) - 2; // index starts at 0 and shave 1 for CR
        Debug_printv("bite_size[%d]", bite_size);
    }

    channel_data.protocol->status(&ns);

    // escape chars that would break up INPUT#
    //mstr::replaceAll(*receiveBuffer[channel], ",", "\",\"");
    //mstr::replaceAll(*receiveBuffer[channel], ";", "\";\"");
    //mstr::replaceAll(*receiveBuffer[channel], ":", "\":\"");
    //mstr::replaceAll(*receiveBuffer[channel], "\r", "\"\r\"");
    //mstr::replaceAll(*receiveBuffer[channel], "\"", "\"\"");
    mstr::replaceAll(channel_data.receiveBuffer, "\"", "");

    // break up receiveBuffer[channel] into bites less than bite_size bytes
    std::string bites = "\"";
    bites.reserve(channel_data.receiveBuffer.size() + (channel_data.receiveBuffer.size() / bite_size));

    int start = 0;
    int end = 0;
    int len = 0;
    int count = 0;
    do
    {
        start = end;

        // Set remaining length
        len = channel_data.receiveBuffer.size() - start;
        if ( len > bite_size )
            len = bite_size;

        // Don't make extra bites!
        end = channel_data.receiveBuffer.find('\r', start);
        if ( end == std::string::npos )
            end = start + len; // None found so set end

        // Take a bite
        Debug_printv("start[%d] end[%d] len[%d] bite_size[%d]", start, end, len, bite_size);
        std::string bite = channel_data.receiveBuffer.substr(start, len);
        bites += bite;
        Debug_printv("bite[%s]", bite.c_str());

        // Add CR if there isn't one already
        if ( len == bite_size)
             bites += "\r\"";

        count++;
    } while ( end < channel_data.receiveBuffer.size() );
 
    //bites += "\"";
    //Debug_printv("[%s]", bites.c_str());
    channel_data.receiveBuffer = bites;
}

void iecNetwork::set_translation_mode()
{
    if (pt.size() < 2)
    {
        Debug_printf("no channel\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "no channel specified";
        return;
    }
    else if (pt.size() < 3)
    {
        Debug_printf("no mode\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "no mode specified";
    }

    int channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    channel_data.translationMode = atoi(pt[2].c_str());

    if (!channel_data.protocol)
    {
        iecStatus.error = NETWORK_ERROR_NOT_CONNECTED;
        iecStatus.connected = 0;
    }

    iecStatus.channel = channel;

    switch (channel_data.translationMode)
    {
    case 0:
        iecStatus.msg = "no translation";
        break;
    case 1:
        iecStatus.msg = "atascii<->ascii CR";
        break;
    case 2:
        iecStatus.msg = "atascii<->ascii LF";
        break;
    case 3:
        iecStatus.msg = "atascii<->ascii CRLF";
        break;
    case 4:
        iecStatus.msg = "petscii<->ascii";
        break;
    }

    Debug_printf("Translation mode for channel %u is now %u\r\n", channel, channel_data.translationMode);
}

void iecNetwork::iec_listen_command()
{
}

void iecNetwork::iec_talk_command()
{
    NetworkStatus ns;

    if (!active_status_channel)
    {
        Debug_printf("No active status channel\n");
        IEC.senderTimeout();
        return;
    }
    else if (!network_data_map[active_status_channel].protocol)
    {
        Debug_printf("No active protocol\n");
        IEC.senderTimeout();
        return;
    }

    auto& channel_data = network_data_map[active_status_channel];

    if (channel_data.channelMode == NetworkData::PROTOCOL) {
        channel_data.protocol->status(&ns);
    } else {
        channel_data.json->status(&ns);
    }

    if (is_binary_status) {
        uint8_t binaryStatus[4];

        binaryStatus[0] = ns.rxBytesWaiting & 0xFF;        // Low byte of ns.rxBytesWaiting
        binaryStatus[1] = (ns.rxBytesWaiting >> 8) & 0xFF; // High byte of ns.rxBytesWaiting

        binaryStatus[2] = ns.connected;
        binaryStatus[3] = ns.error;

        Debug_printf("Sending status binary data for active channel: %d, %s\n", active_status_channel, mstr::toHex(binaryStatus, 4).c_str());

        IEC.sendBytes((const char *)binaryStatus, sizeof(binaryStatus), true);
    } else {
        char tmp[32];
        memset(tmp, 0, sizeof(tmp));
        sprintf(tmp, "%u,%u,%u", ns.rxBytesWaiting, ns.connected, ns.error);

        Debug_printf("Sending status %s\n", tmp);

        IEC.sendBytes(tmp, strlen(tmp), true);
    }
}

void iecNetwork::iec_command()
{
    // Check pt size before proceeding to avoid a crash
    if (pt.size()==0) {
        Debug_printf("pt.size()==0!\n");
        return;
    }

    Debug_printf("pt[0]=='%s'\n", pt[0].c_str());
    if (pt[0] == "cd")
        set_prefix();
    else if (pt[0] == "chmode")
        set_channel_mode();
    else if (pt[0] == "openparams")
        set_open_params();
    else if (pt[0] == "status")
        set_status(false);
    else if (pt[0] == "statusb")
        set_status(true);
    else if (pt[0] == "id")
        set_device_id();
    else if (pt[0] == "jsonparse")
        parse_json();
    else if (pt[0] == "jq")
        query_json();
    else if (pt[0] == "biteparse")
        parse_bite();
    else if (pt[0] == "settrans")
        set_translation_mode();
    else if (pt[0] == "pwd")
        get_prefix();
    else if (pt[0] == "login")
        set_login_password();
    else if (pt[0] == "rename" || pt[0] == "ren")
        fsop(0x20);
    else if (pt[0] == "delete" || pt[0] == "del" || pt[0] == "rm")
        fsop(0x21);
    else if (pt[0] == "lock")
        fsop(0x23);
    else if (pt[0] == "unlock")
        fsop(0x24);
    else if (pt[0] == "mkdir")
        fsop(0x2A);
    else if (pt[0] == "rmdir")
        fsop(0x2B);
    else // Protocol command processing here.
    {
        if (pt.size() > 1)
        {
            uint8_t channel = atoi(pt[1].c_str());

            // This assumption is safe, because no special commands should ever
            // be done on channel 0 (or 1 for that matter.) -tschak
            if (!channel)
                return;

            auto& channel_data = network_data_map[channel];

            if (!channel_data.protocol)
            {
                Debug_printv("ERROR: trying to perform command on channel without a protocol. channel = %d, payload = >%s<\r\n", channel, payload.c_str());
                return;
            }

            Debug_printv("pt[0][0]=[%2X] pt[1]=[%d] aux1[%d] aux2[%d]", pt[0][0], channel, cmdFrame.aux1, cmdFrame.aux2);

            if (channel_data.protocol->special_inquiry(pt[0][0]) == 0x00)
                perform_special_00();
            else if (channel_data.protocol->special_inquiry(pt[0][0]) == 0x40)
                perform_special_40();
            else if (channel_data.protocol->special_inquiry(pt[0][0]) == 0x80)
                perform_special_80();
        }
    }
}

void iecNetwork::perform_special_00()
{
    int channel = 0;

    if (pt.size() > 0)
        cmdFrame.comnd = pt[0][0];

    if (pt.size() > 1)
        channel = atoi(pt[1].c_str());

    if (pt.size() > 2)
        cmdFrame.aux1 = atoi(pt[2].c_str());

    if (pt.size() > 3)
        cmdFrame.aux2 = atoi(pt[3].c_str());

    auto& channel_data = network_data_map[channel];

    if (channel_data.protocol->special_00(&cmdFrame))
    {
        NetworkStatus ns;
        char reply[80];
        string s;

        channel_data.protocol->status(&ns);
        snprintf(reply, 80, "protocol error #%u", ns.error);
        iecStatus.error = ns.error;
        iecStatus.channel = commanddata.channel; // shouldn't this be "channel"
        iecStatus.connected = ns.connected;
        s = string(reply);
        iecStatus.msg = s; // mstr::toPETSCII2(s);
    }
}

void iecNetwork::perform_special_40()
{
    char sp_buf[256];
    int channel = 0;
    NetworkStatus ns;

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = 15;
        iecStatus.connected = 0;
        iecStatus.msg = "no channel #";
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    cmdFrame.comnd = pt[0][0];

    if (pt.size() < 3)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = channel;
        iecStatus.connected = 0;
        iecStatus.msg = "no aux1";
    }

    cmdFrame.aux1 = atoi(pt[2].c_str());

    if (pt.size() < 4)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = channel;
        iecStatus.connected = 0;
        iecStatus.msg = "no aux2";
    }

    cmdFrame.aux2 = atoi(pt[3].c_str());

    if (!channel_data.protocol)
    {
        iecStatus.error = NETWORK_ERROR_NOT_CONNECTED;
        iecStatus.channel = 15;
        iecStatus.connected = 0;
        iecStatus.msg = "no active protocol";
        return;
    }

    if (channel_data.protocol->special_40((uint8_t *)&sp_buf, sizeof(sp_buf), &cmdFrame))
    {
        channel_data.protocol->status(&ns);
        iecStatus.error = ns.error;
        iecStatus.connected = ns.connected;
        iecStatus.channel = channel;
        iecStatus.msg = "protocol read error";
        return;
    }
    else
    {
        channel_data.protocol->status(&ns);
        iecStatus.error = ns.error;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = string(sp_buf);
    }
}

void iecNetwork::perform_special_80()
{
    string sp_buf = "N:";
    int channel = 0;
    NetworkStatus ns;

    Debug_printf("perform_special_80()\n");

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = 15;
        iecStatus.connected = 0;
        iecStatus.msg = "no channel #";
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    if (pt.size() < 3)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = 15;
        iecStatus.connected = 0;
        iecStatus.msg = "no aux1";
        return;
    }

    cmdFrame.aux1 = atoi(pt[2].c_str());

    if (pt.size() < 4)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = 15;
        iecStatus.connected = 0;
        iecStatus.msg = "no aux2";
        return;
    }

    cmdFrame.aux2 = atoi(pt[3].c_str());

    if (pt.size() < 5)
    {
        if (channel_data.protocol)
        {
            channel_data.protocol->status(&ns);
            iecStatus.error = ns.error;
            iecStatus.connected = ns.connected;
        }
        else
        {
            iecStatus.error = NETWORK_ERROR_NOT_CONNECTED;
            iecStatus.connected = 0;
        }

        iecStatus.channel = channel;
        iecStatus.msg = "parameter missing";
    }

    cmdFrame.comnd = pt[0][0];
    sp_buf += pt[4];

    if (channel_data.protocol->special_80((uint8_t *)sp_buf.c_str(), sp_buf.length(), &cmdFrame))
    {
        channel_data.protocol->status(&ns);
        iecStatus.error = ns.error;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "error";
    }
    else
    {
        channel_data.protocol->status(&ns);
        iecStatus.error = ns.error;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "ok";
    }
}

void iecNetwork::set_channel_mode()
{
    NetworkStatus ns;

    if (pt.size() < 3)
    {
        Debug_printf("set_channel_mode no channel or mode specified");
        iecStatus.error = ns.error;
        iecStatus.msg = "no channel or mode specified";
        iecStatus.connected = ns.connected;
        iecStatus.channel = commanddata.channel;
        return;
    }
    else if (pt.size() < 3)
    {
        Debug_printf("set_channel_mode no mode specified for channel %u\r\n", atoi(pt[1].c_str()));
        iecStatus.error = ns.error;
        iecStatus.msg = "no mode specified for channel " + pt[1];
        iecStatus.connected = ns.connected;
        iecStatus.channel = commanddata.channel;
    }
    else
    {
        int channel = atoi(pt[1].c_str());
        auto& channel_data = network_data_map[channel];

        string newMode = pt[2];

        if (newMode == "json")
            channel_data.channelMode = NetworkData::JSON;
        else if (newMode == "protocol")
            channel_data.channelMode = NetworkData::PROTOCOL;

        Debug_printf("Channel mode set to %s %u\r\n", newMode.c_str(), channel_data.channelMode);
        iecStatus.error = ns.error;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "channel mode set to " + newMode;
    }
}

void iecNetwork::get_prefix()
{
    int channel = -1;

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        iecStatus.msg = "need channel #";
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.msg = channel_data.prefix;
    iecStatus.connected = 0;
    iecStatus.channel = channel;
}

void iecNetwork::set_status(bool is_binary)
{
    is_binary_status = is_binary;
    if (pt.size() < 2)
    {
        Debug_printf("Channel # Required\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "channel # required.\n";
        iecStatus.connected = 0;
        iecStatus.channel = 15;
        return;
    }

    active_status_channel = atoi(pt[1].c_str());
    Debug_printf("Active status channel now: %u\n", active_status_channel);

    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.msg = "Active status channel set.";
    iecStatus.channel = active_status_channel;
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
        Debug_printf("Channel # required\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "channel # required";
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        return;
    }
    else if (pt.size() == 2) // clear prefix
    {
        Debug_printf("Prefix cleared\r\n");
        channel = atoi(pt[1].c_str());
        auto& channel_data = network_data_map[channel];

        channel_data.prefix.clear();
        channel_data.prefix.shrink_to_fit();
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
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

    util_devicespec_fix_9b(prefixSpec, sizeof(prefixSpec));

    prefixSpec_str = string((const char *)prefixSpec);
    Debug_printf("iecNetwork::set_prefix(%s)\r\n", prefixSpec_str.c_str());

    auto& channel_data = network_data_map[channel];

    if (prefixSpec_str == "..") // Devance path N:..
    {
        std::vector<int> pathLocations;
        for (int i = 0; i < channel_data.prefix.size(); i++)
        {
            if (channel_data.prefix[i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (channel_data.prefix[channel_data.prefix.size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        channel_data.prefix = channel_data.prefix.substr(0, pathLocations.back() + 1);
    }
    else if (prefixSpec_str == "/") // Go back to hostname.
    {
        // TNFS://foo.com/path
        size_t pos = channel_data.prefix.find("/");

        if (pos == string::npos)
        {
            channel_data.prefix.clear();
            channel_data.prefix.shrink_to_fit();
        }

        pos = channel_data.prefix.find("/", ++pos);

        if (pos == string::npos)
        {
            channel_data.prefix.clear();
            channel_data.prefix.shrink_to_fit();
        }

        pos = channel_data.prefix.find("/", ++pos);

        if (pos == string::npos)
            channel_data.prefix += "/";

        pos = channel_data.prefix.find("/", ++pos);

        channel_data.prefix = channel_data.prefix.substr(0, pos);
    }
    else if (prefixSpec_str[0] == '/') // N:/DIR
    {
        channel_data.prefix = prefixSpec_str;
    }
    else if (prefixSpec_str.empty())
    {
        channel_data.prefix.clear();
    }
    else if (prefixSpec_str.find_first_of(":") != string::npos)
    {
        channel_data.prefix = prefixSpec_str;
    }
    else // append to path.
    {
        channel_data.prefix += prefixSpec_str;
    }

    channel_data.prefix = util_get_canonical_path(channel_data.prefix);

    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.msg = channel_data.prefix;
    iecStatus.connected = 0;
    iecStatus.channel = channel;

    Debug_printf("Prefix now: %s\r\n", channel_data.prefix.c_str());
}

void iecNetwork::set_device_id()
{
    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "device id required";
        return;
    }

    int new_id = atoi(pt[1].c_str());

    IEC.changeDeviceId(this, new_id);

    iecStatus.error = 0;
    iecStatus.msg = "ok";
    iecStatus.connected = 0;
    iecStatus.channel = commanddata.channel;
}

void iecNetwork::fsop(unsigned char comnd)
{
    Debug_printf("fsop(%u)\r\n", comnd);

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "invalid # of parameters";
        return;
    }

    // Overwrite payload, we no longer need command
    payload = pt[1];

    iec_open();

    cmdFrame.comnd = comnd;

    int channel = commanddata.channel;
    auto& channel_data = network_data_map[channel];

    if (channel_data.protocol)
        channel_data.protocol->perform_idempotent_80(channel_data.urlParser.get(), &cmdFrame);

    iec_close();
}

void iecNetwork::set_open_params()
{
    // openparams,<channel>,<mode>,<trans>
    if (pt.size() < 3)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "invalid # of parameters";
        return;
    }
    
    int channel = atoi(pt[1].c_str());
    int mode = atoi(pt[2].c_str());
    int trans = atoi(pt[3].c_str());

    auto& channel_data = network_data_map[channel];

    channel_data.protocol->set_open_params(mode, trans);

    iecStatus.error = 0;
    iecStatus.msg = "ok";
    iecStatus.connected = 0;
    iecStatus.channel = channel;

}

device_state_t iecNetwork::process()
{
    // Call base class
    virtualDevice::process();
    //payload=mstr::toUTF8(payload); // @idolpx? What should I do instead?

    mstr::rtrim(payload);

    // Debug_printv("commanddata: prim:%02x, dev:%02x, 2nd:%02x, chan:%02x\r\n", commanddata.primary, commanddata.device, commanddata.secondary, commanddata.channel);

    // fan out to appropriate process routine
    switch (commanddata.channel)
    {
    case CHANNEL_LOAD:
        // Debug_printv("[CHANNEL_LOAD]");
        process_load();
        break;
    case CHANNEL_SAVE:
        // Debug_printv("[CHANNEL_SAVE]");
        process_save();
        break;
    case CHANNEL_COMMAND:
        // Debug_printv("[CHANNEL_COMMAND]");
        process_command();
        break;
    default:
        // Debug_printv("[DEFAULT - PROCESS_CHANNEL]");
        process_channel();
        break;
    }

    return state;
}

void iecNetwork::process_load()
{
    Debug_printv("secondary[%2X]", commanddata.secondary);
    switch (commanddata.secondary)
    {
    case IEC_OPEN:
        // Debug_printv("[IEC_OPEN (LOAD)]");
        iec_open();
        break;
    case IEC_CLOSE:
        // Debug_printv("[IEC_CLOSE (LOAD)]");
        iec_close();
        break;
    case IEC_REOPEN:
        // Debug_printv("[IEC_REOPEN (LOAD)]");
        iec_reopen_load();
        break;
    default:
        break;
    }
}

void iecNetwork::process_save()
{
    Debug_printv("secondary[%2X]", commanddata.secondary);
    switch (commanddata.secondary)
    {
    case IEC_OPEN:
        // Debug_printv("[IEC_OPEN (SAVE)]");
        iec_open();
        break;
    case IEC_CLOSE:
        // Debug_printv("[IEC_CLOSE (SAVE)]");
        iec_close();
        break;
    case IEC_REOPEN:
        // Debug_printv("[IEC_REOPEN (SAVE)]");
        iec_reopen_save();
        break;
    default:
        break;
    }
}

void iecNetwork::process_channel()
{
    Debug_printv("secondary[%2X]", commanddata.secondary);

    // we're double processing on the IEC_LISTEN and IEC_UNLISTEN phases for an OPEN. Only do the open on the UNLISTEN
    if (!(commanddata.primary == IEC_LISTEN && commanddata.secondary == IEC_OPEN)) {
        switch (commanddata.secondary)
        {
        case IEC_OPEN:
            // Debug_printv("[IEC_OPEN (CHANNEL)]");
            iec_open();
            break;
        case IEC_CLOSE:
            // Debug_printv("[IEC_CLOSE (CHANNEL)]");
            iec_close();
            break;
        case IEC_REOPEN:
            // Debug_printv("[IEC_REOPEN (CHANNEL)]");
            iec_reopen_channel();
            break;
        default:
            break;
        }
    } else {
        Debug_printv("SKIPPING process_channel prim: %02x, 2nd: %02x", commanddata.primary, commanddata.secondary);
        return;
    }
}

void iecNetwork::process_command()
{
    Debug_printv("primary[%2X]", commanddata.primary);
    switch (commanddata.primary)
    {
    case IEC_LISTEN:
        // Debug_printv("[IEC_LISTEN]");
        // Debug_printv("FIXING PAYLOAD DATA TO ASCII\r\n");
        clean_transform_petscii_to_ascii(payload);
        pt = util_tokenize(payload, ',');
        break;
    case IEC_TALK:
        // Debug_printv("[IEC_TALK]");
        iec_talk_command();
        break;
    case IEC_UNLISTEN:
        // Debug_printv("[IEC_UNLISTEN - CALLING iec_command()]");
        iec_command();
        break;
    default:
        break;
    }

}

#endif /* BUILD_IEC */