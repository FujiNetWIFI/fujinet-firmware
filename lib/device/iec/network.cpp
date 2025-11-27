#ifdef BUILD_IEC
/**
 * N: Firmware
 */

#include <algorithm>
#include <cstring>
#include <cstdint>

#include "network.h"
#include "../network.h"
#include "../../include/cbm_defines.h"

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


iecNetwork::iecNetwork(uint8_t devnr) : IECFileDevice(devnr)
{
  init();
}


iecNetwork::~iecNetwork()
{
}


void iecNetwork::init()
{
  iecStatus.channel = CHANNEL_COMMAND;
  iecStatus.connected = 0;
  iecStatus.msg = "fujinet network device";
  iecStatus.error = NETWORK_ERROR_SUCCESS;

  commanddata.init();
  active_status_channel = 0;
  is_binary_status = false;
}


void iecNetwork::iec_open()
{
    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId]; // This will create the channel if it doesn't exist

    uint8_t channel_aux1 = 12;
    uint8_t channel_aux2 = channel_data.translationMode; // not sure about this, you can't set this unless you send a command for the channel first, I think it relies on the array being init to 0s

    file_not_found = false;

    channel_data.deviceSpec.clear();
    if (!channel_data.prefix.empty()) {
        channel_data.deviceSpec += channel_data.prefix;
    }

    Debug_printf("%s", util_hexdump(payload.c_str(), payload.length()).c_str());

    // Check if the payload is RAW (i.e. from fujinet-lib) by the presence of "01" as the first uint8_t, which can't happen for BASIC.
    // If it is, then the next 2 bytes are the aux1/aux2 values (mode and trans), and the rest is the actual URL.
    // This is an efficiency so we don't have to send a 2nd command to tell it what the parameters should have been.
    // BASIC will still need to use "openparams" command, as the OPEN line doesn't have capacity for the parameters (can't use a "," as that's a valid URL character)
    if (payload[0] == 0x01) {
        channel_aux1 = payload[1];
        channel_aux2 = payload[2];

        // capture the trans mode as though "settrans" had been invoked
        channel_data.translationMode = channel_aux2;
        // translationMode[commanddata.channel] = channel_aux2;

        // remove the marker bytes so the payload can continue as with BASIC setup
        if (payload.length() > 3)
            payload = payload.substr(3);
    }

    if (payload != "$") {
        clean_transform_petscii_to_ascii(payload);
        Debug_printv("transformed payload to %s", payload.c_str());
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
    Debug_printv("Creating protocol for schema %s", channel_data.urlParser->scheme.c_str());
    channel_data.protocol = std::move(NetworkProtocolFactory::createProtocol(channel_data.urlParser->scheme, channel_data));

    if (!channel_data.protocol) {
        Debug_printf("Invalid protocol: %s", channel_data.urlParser->scheme.c_str());

        iecStatus.error = NETWORK_ERROR_FILE_NOT_FOUND;
        iecStatus.msg = "file not found";
        iecStatus.connected = 0;
        iecStatus.channel = commanddata.channel;

        file_not_found = true; // Assuming file_not_found is accessible here
        return;
    }

    // Set login and password if they exist
    if (!channel_data.login.empty()) {
        // TODO: Change the NetworkProtocol password and login to STRINGS FFS
        channel_data.protocol->login = &channel_data.login;
        channel_data.protocol->password = &channel_data.password;
    }

    Debug_printv("Protocol %s opened.", channel_data.urlParser->scheme.c_str());

    if (channel_data.protocol->open(channel_data.urlParser.get(), (netProtoOpenMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2)) {
        Debug_printv("Protocol unable to make connection.");
        channel_data.protocol.reset(); // Clean up the protocol

        iecStatus.error = NETWORK_ERROR_FILE_NOT_FOUND;
        iecStatus.msg = "file not found";
        iecStatus.connected = 0;
        iecStatus.channel = commanddata.channel;

        file_not_found = true;
        return;
    }

    channel_data.json = std::make_unique<FNJSON>();
    channel_data.json->setProtocol(channel_data.protocol.get());

    if( channel_data.protocol->interruptEnable ) sendSRQ();
}


void iecNetwork::iec_close()
{
    Debug_printf("iecNetwork::iec_close(), channel #%d", commanddata.channel);

    int channelId = commanddata.channel;
    auto& channel_data = network_data_map[channelId];

    /*
    // setting this status wipes out any other error status set previously
    iecStatus.channel = commanddata.channel;
    iecStatus.error = NETWORK_ERROR_SUCCESS;
    iecStatus.connected = 0;
    iecStatus.msg = "closed";
    */

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
        Debug_printf("parse_json - no channel specified");
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
        Debug_printf("could not parse json");
        iecStatus.error = NETWORK_ERROR_GENERAL;
        iecStatus.channel = channel;
        iecStatus.connected = ns.connected;
        iecStatus.msg = "could not parse json";
    }
    else
    {
        Debug_printf("json parsed");
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

    Debug_printf("query_json(%s)", payload.c_str());

    if (pt.size() < 2)
    {
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "invalid # of parameters";
        iecStatus.channel = 0;
        iecStatus.connected = 0;
        Debug_printf("Invalid # of parameters to set_json_query()");
        return;
    }

    channel = atoi(pt[1].c_str());
    auto& channel_data = network_data_map[channel];

    s = pt.size() == 2 ? "" : pt[2];  // allow empty string if there aren't enough args

    Debug_printf("Channel: %u", channel);

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
    Debug_printf("Query set to %s", s.c_str());
}

void iecNetwork::parse_bite()
{
    int channel = 0;
    int bite_size = 79;
    NetworkStatus ns;

    if (pt.size() < 2)
    {
        Debug_printf("parse_bite - no channel specified");
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
    channel_data.receiveBuffer = mstr::toPETSCII2(bites);
}

void iecNetwork::set_translation_mode()
{
    if (pt.size() < 2)
    {
        Debug_printf("no channel");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.channel = commanddata.channel;
        iecStatus.connected = 0;
        iecStatus.msg = "no channel specified";
        return;
    }
    else if (pt.size() < 3)
    {
        Debug_printf("no mode");
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

    Debug_printf("Translation mode for channel %u is now %u", channel, channel_data.translationMode);
}

void iecNetwork::iec_command()
{
    // Check pt size before proceeding to avoid a crash
    if (pt.size()==0) {
        Debug_printf("pt.size()==0!\r\n");
        return;
    }

    Debug_printv("pt[0]=='%s'\r\n", pt[0].c_str());
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
                Debug_printv("ERROR: trying to perform command on channel without a protocol. channel = %d, payload = >%s<", channel, payload.c_str());
                return;
            }

            AtariSIODirection m = channel_data.protocol->special_inquiry((fujiCommandID_t) pt[0][0]);
            Debug_printv("pt[0][0]=[%2X] pt[1]=[%d] size[%d] m[%d]", pt[0][0], channel, pt.size(), m);
            if (m == SIO_DIRECTION_NONE)
                perform_special_00();
            else if (m == SIO_DIRECTION_READ)
                perform_special_40();
            else if (m == SIO_DIRECTION_WRITE)
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

    if (channel_data.protocol->special_00((fujiCommandID_t) cmdFrame.comnd, cmdFrame.aux2))
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

    if (channel_data.protocol->special_40((uint8_t *)&sp_buf, sizeof(sp_buf), (fujiCommandID_t) cmdFrame.comnd))
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

    Debug_printf("perform_special_80()\r\n");

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

    if (channel_data.protocol->special_80((uint8_t *)sp_buf.c_str(), sp_buf.length(), (fujiCommandID_t) cmdFrame.comnd))
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
        Debug_printf("set_channel_mode no mode specified for channel %u", atoi(pt[1].c_str()));
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

        Debug_printf("Channel mode set to %s %u", newMode.c_str(), channel_data.channelMode);
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
        Debug_printf("Channel # Required\r\n");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "channel # required.\r\n";
        iecStatus.connected = 0;
        iecStatus.channel = 15;
        return;
    }

    active_status_channel = atoi(pt[1].c_str());
    Debug_printf("Active status channel now: %u\r\n", active_status_channel);

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
        Debug_printf("Channel # required");
        iecStatus.error = NETWORK_ERROR_INVALID_DEVICESPEC;
        iecStatus.msg = "channel # required";
        iecStatus.connected = 0;
        iecStatus.channel = channel;
        return;
    }
    else if (pt.size() == 2) // clear prefix
    {
        Debug_printf("Prefix cleared");
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
    Debug_printf("iecNetwork::set_prefix(%s)", prefixSpec_str.c_str());

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

    Debug_printf("Prefix now: %s", channel_data.prefix.c_str());
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
    setDeviceNumber(new_id);

    iecStatus.error = 0;
    iecStatus.msg = "ok";
    iecStatus.connected = 0;
    iecStatus.channel = commanddata.channel;
}

void iecNetwork::fsop(unsigned char comnd)
{
    Debug_printf("fsop(%u)", comnd);

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
        channel_data.protocol->perform_idempotent_80(channel_data.urlParser.get(), (fujiCommandID_t) cmdFrame.comnd);

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
    netProtoTranslation_t trans = (netProtoTranslation_t) (atoi(pt[3].c_str()) & 0x7F);

    auto& channel_data = network_data_map[channel];

    channel_data.protocol->set_open_params(trans);

    iecStatus.error = 0;
    iecStatus.msg = "ok";
    iecStatus.connected = 0;
    iecStatus.channel = channel;

}


bool iecNetwork::open(uint8_t channel, const char *name)
{
  Debug_printv("iecNetwork::open(#%d, %d, \"%s\")", m_devnr, channel, name);

  commanddata.channel = channel;
  payload = std::string(name);
  iec_open();
  clearStatus();

  return true;
}


void iecNetwork::close(uint8_t channel)
{
  Debug_printv("iecNetwork::close(#%d, %d)", m_devnr, channel);

  commanddata.channel = channel;
  iec_close();
  clearStatus();
}


bool iecNetwork::transmit(NetworkData &channel_data)
{
  if (!channel_data.protocol)
    {
      Debug_printf("iec_reopen_channel_listen() - Not connected");
      return false;
    }

  // force incoming data from HOST to fixed ascii
  // Debug_printv("[1] DATA: >%s< [%s]", channel_data.transmitBuffer.c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());
  clean_transform_petscii_to_ascii(channel_data.transmitBuffer);
  // Debug_printv("[2] DATA: >%s< [%s]", channel_data.transmitBuffer.c_str(), mstr::toHex(channel_data.transmitBuffer).c_str());

  Debug_printf("Received %u bytes. Transmitting.", channel_data.transmitBuffer.length());

  channel_data.protocol->write(channel_data.transmitBuffer.length());
  channel_data.transmitBuffer.clear();
  channel_data.transmitBuffer.shrink_to_fit();
  return true;
}


bool iecNetwork::receive(NetworkData &channel_data, uint16_t rxBytes)
{
  NetworkStatus ns;

  if (!channel_data.protocol)
    {
      //Debug_printv("No protocol set");
      return false;
    }

  if (file_not_found)
    {
      Debug_printv("file not found");
      return false;
    }

  // Get status
  channel_data.protocol->status(&ns);
  size_t avail = channel_data.protocol->available();
  if( avail > 0 )
    {
      uint16_t blockSize = std::min(avail, (size_t) rxBytes);
      Debug_printf("bytes waiting: %u / blockSize: %u / connected: %u / error: %u ", avail, blockSize, ns.connected, ns.error);
      if( channel_data.protocol->read(blockSize) )
        {
          // protocol adapter returned error
          iecStatus.error = NETWORK_ERROR_GENERAL;
          iecStatus.msg = "read error";
          iecStatus.connected = ns.connected;
          iecStatus.channel = commanddata.channel;
          Debug_printv("Read Error");
          return false;
        }
    }

  return true;
}


uint8_t iecNetwork::write(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool eoi)
{
  if( bufferSize==0 ) return 0;

  //Debug_printv("iecNetwork::write(#%d, %d, %d) = %s", m_devnr, channel, bufferSize, mstr::toHex(buffer, bufferSize).c_str());

  int channelId = commanddata.channel;
  auto& channel_data = network_data_map[channelId];

  channel_data.transmitBuffer = string((char *) buffer, bufferSize);
  return transmit(channel_data) ? bufferSize : 0;
}


uint8_t iecNetwork::read(uint8_t channel, uint8_t *buffer, uint8_t bufferSize, bool *eoi)
{
  int channelId = commanddata.channel;
  auto& channel_data = network_data_map[channelId];

  if( channel_data.receiveBuffer.size() < bufferSize )
    if( !receive(channel_data, 2048) )
      return 0;

  uint8_t n = std::min((int) channel_data.receiveBuffer.size(), (int) bufferSize);
  memcpy(buffer, channel_data.receiveBuffer.data(), n);
  channel_data.receiveBuffer.erase(0, n);

  //if( n>0 ) Debug_printv("iecNetwork::read(#%d, %d, %d)", m_devnr, channel, bufferSize);
  return n;
}


void iecNetwork::execute(const char *cmd, uint8_t cmdLen)
{
  Debug_printv("iecNetwork::execute(#%d, \"%s\", %d)", m_devnr, cmd, cmdLen);

  payload = std::string(cmd, cmdLen);
  clean_transform_petscii_to_ascii(payload);
  pt = util_tokenize(payload, ',');
  iec_command();
  clearStatus();
}


uint8_t iecNetwork::getStatusData(char *buffer, uint8_t bufferSize)
{
  Debug_printv("iecNetwork::getStatusData(#%d, %d)", m_devnr, bufferSize);

  if( !active_status_channel || !network_data_map[active_status_channel].protocol )
    {
      if( is_binary_status )
        {
          if (!active_status_channel)
            Debug_printf("No active status channel\r\n");

          if( !network_data_map[active_status_channel].protocol )
            Debug_printf("No active protocol\r\n");

          return 0;
        }
      else
        {
          Debug_printf("msg: %s\r\n", iecStatus.msg.c_str());
          util_petscii_to_ascii_str(iecStatus.msg); // are the util pescii/asccii functions reversed?
          Debug_printf("msgPETSCII: %s\r\n", iecStatus.msg.c_str());
          snprintf(buffer, bufferSize, "%d,%s,%02d,%02d\r\n",
                   iecStatus.error, iecStatus.msg.c_str(), iecStatus.channel, iecStatus.connected);

          Debug_printf("Sending status: %s\r\n", buffer);

          // reset status
          iecStatus.error = 0;
          iecStatus.channel = 0;
          iecStatus.connected = 0;
          iecStatus.msg = "ok";

          return strlen(buffer);
        }
    }
  else
    {
      NetworkStatus ns;
      auto& channel_data = network_data_map[active_status_channel];

      if (channel_data.channelMode == NetworkData::PROTOCOL) {
        channel_data.protocol->status(&ns);
      } else {
        channel_data.json->status(&ns);
      }

      size_t avail = channel_data.protocol->available();
      avail = avail > 65535 ? 65535 : avail;

      if (is_binary_status) {
        NDeviceStatus *status = (NDeviceStatus *) buffer;
        status->avail = avail;
        status->conn = ns.connected;
        status->err = ns.error;

        Debug_printf("Sending binary status for active channel #%d: %s\r\n", active_status_channel, mstr::toHex((uint8_t *) buffer, 4).c_str());
        return 4;
      } else {
        snprintf(buffer, bufferSize, "%u,%u,%u", avail, ns.connected, ns.error);
        Debug_printf("Sending status for active channel #%d: %s\r\n", active_status_channel, buffer);
        return strlen(buffer);
      }
    }
}


void iecNetwork::reset()
{
  Debug_printv("iecNetwork::reset()");

  // close all channels
  for(auto it=network_data_map.begin(); it!=network_data_map.end(); it++)
    {
      commanddata.channel = it->first;
      iec_close();
    }
  network_data_map.clear();

  // re-initialize internal states
  init();

  // process reset in parent class
  IECFileDevice::reset();
}


void iecNetwork::task()
{
  IECFileDevice::task();

  static uint32_t nextSRQ = 0;
  NetworkStatus ns;

  if( fnSystem.millis()>=nextSRQ )
    {
      for(auto it=network_data_map.begin(); it!=network_data_map.end(); it++)
        {
          auto& protocol = it->second.protocol;
          if( protocol && protocol->interruptEnable )
            {
              protocol->status(&ns);
              if( protocol->available() > 0 /*|| ns.connected == 0*/ )
                {
                  sendSRQ();
                  nextSRQ = fnSystem.millis() + 10;
                }
            }
        }
    }
}


#endif /* BUILD_IEC */
