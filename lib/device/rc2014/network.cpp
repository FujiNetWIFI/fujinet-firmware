#ifdef BUILD_RC2014

/**
 * N: Firmware
 */

#include "network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"

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

#include "ProtocolParser.h"

//using namespace std;

/**
 * Constructor
 */
rc2014Network::rc2014Network()
{
    status_response[1] = 0x00;
    status_response[2] = 0x04; // 1024 bytes
    status_response[3] = 0x00; // Character device

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
rc2014Network::~rc2014Network()
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

/** rc2014 COMMANDS ***************************************************************/

/**
 * rc2014 Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void rc2014Network::open()
{
    Debug_printf("rc2014Network::open()\n");

    string d;

    rc2014_send_ack();

    memset(response,0,sizeof(response));
    rc2014_recv_buffer(response, 256);

    Debug_printf("rc2014Network::open url %s\n", response);
    
    rc2014_send_ack();

    channelMode = PROTOCOL;

    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;

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
    network_status.reset();

    Debug_printf("open()\n");

    // Parse and instantiate protocol
    d=string((char *)response,256);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), &cmdFrame) == true)
    {
        network_status.error = protocol->error;
        Debug_printf("Protocol unable to make connection. Error: %d\n", protocol->error);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        return;
    }

    json.setProtocol(protocol);

    rc2014_send_complete();
}

/**
 * rc2014 Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void rc2014Network::close()
{
    Debug_printf("rc2014Network::close()\n");

    rc2014_send_ack();

    network_status.reset();

    if (protocolParser != nullptr)
    {
        delete protocolParser;
        protocolParser = nullptr;
    }
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

    rc2014_send_complete();
}

/**
 * @brief Perform read of the current JSON channel
 * @param num_bytes Number of bytes to read
 */
bool rc2014Network::read_channel_json(unsigned short num_bytes)
{
    if (num_bytes > json_bytes_remaining)
        json_bytes_remaining=0;
    else
        json_bytes_remaining-=num_bytes;

    return false;
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool rc2014Network::read_channel(unsigned short num_bytes)
{
    bool _err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        _err = protocol->read(num_bytes);
        break;
    case JSON:
        err = read_channel_json(num_bytes);
        break;
    }
    return _err;
}

/**
 * rc2014 Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to rc2014. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void rc2014Network::write()
{
    Debug_printf("rc2014Network::write()\n");

    memset(response, 0, sizeof(response));
    rc2014_send_ack();

    uint16_t num_bytes = (cmdFrame.aux2 << 8) + cmdFrame.aux1;

    rc2014_recv_buffer(response, num_bytes);
    rc2014_send_ack();

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        network_status.error = NETWORK_ERROR_NOT_CONNECTED;
        rc2014_send_error();
        return;
    }

    *transmitBuffer += string((char *)response, num_bytes);
    err = write_channel(num_bytes);

    rc2014_send_complete();
}

void rc2014Network::read()
{
    Debug_printf("rc2014Network::read()\n");

    uint16_t num_bytes = (cmdFrame.aux2 << 8) + cmdFrame.aux1;
    Debug_printf("rc2014Network::read %u bytes\n", num_bytes);


    rc2014_send_ack();

    // Check for rx buffer. If NULL, then tell caller we could not allocate buffers.
    if (receiveBuffer == nullptr)
    {
        network_status.error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        rc2014_send_error();
        return;
    }

    // If protocol isn't connected, then return not connected.
    if (protocol == nullptr)
    {
        network_status.error = NETWORK_ERROR_NOT_CONNECTED;
        rc2014_send_error();
        return;
    }

    // Do the channel read
    err = read_channel(num_bytes);

    rc2014_send_buffer((uint8_t *)receiveBuffer->data(), num_bytes);
    rc2014_flush();
    receiveBuffer->erase(0, num_bytes);

    Debug_printf("rc2014Network::read sent %u bytes\n", num_bytes);

    rc2014_send_complete();
}

/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit rc2014_error or rc2014_complete().
 */
bool rc2014Network::write_channel(unsigned short num_bytes)
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

bool rc2014Network::status_channel_json(NetworkStatus *ns)
{
    ns->connected = json_bytes_remaining > 0;
    ns->error = json_bytes_remaining > 0 ? 1 : 136;
    ns->rxBytesWaiting = json_bytes_remaining;
    return false; // for now
}

/**
 * rc2014 Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to rc2014.
 */
void rc2014Network::status()
{
    Debug_printf("rc2014Network::status()\n");

    NetworkStatus s;
    
    rc2014_send_ack();

    switch (channelMode)
    {
    case PROTOCOL:
        if (protocol == nullptr) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = true;
            s.error = true;
        } else {
            err = protocol->status(&s);
        }
        break;
    case JSON:
        err = status_channel_json(&s);
        break;
    }

    uint16_t bytes_waiting = (s.rxBytesWaiting > RC2014_TX_BUFFER_SIZE) ?
            RC2014_TX_BUFFER_SIZE : s.rxBytesWaiting;

    response[0] = bytes_waiting & 0xFF;
    response[1] = bytes_waiting >> 8;
    response[2] = s.connected;
    response[3] = s.error;
    response_len = 4;
    //receiveMode = STATUS;

    rc2014_send_buffer(response, response_len);
    rc2014_flush();
    
    rc2014_send_complete();

}

/**
 * JSON functionality
 */
void rc2014Network::rc2014_parse_json()
{
    Debug_printf("rc2014Network::parse_json()\n");

    rc2014_send_ack();

    json.parse();

    rc2014_send_complete();
}

void rc2014Network::rc2014_set_json_query()
{
    uint8_t in[256];
    const char *inp = NULL;
    uint8_t *tmp;
    Debug_printf("rc2014Network::set_json_query()\n");

    memset(in, 0, sizeof(in));

    rc2014_send_ack();

    rc2014_recv_buffer(in, sizeof(in));
    rc2014_send_ack();

    // strip away line endings from input spec.
    for (int i = 0; i < 256; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    inp = (const char *)in;
    Debug_printf("#1 %s\n",inp);
    json.setReadQuery(string(inp),cmdFrame.aux2);
    json_bytes_remaining = json.readValueLen();
    tmp = (uint8_t *)malloc(json.readValueLen());
    json.readValue(tmp,json_bytes_remaining);
    *receiveBuffer += string((const char *)tmp,json_bytes_remaining);
    free(tmp);

    Debug_printf("Query set to %s\n",inp);
    rc2014_send_complete();
}


/**
 * @brief set channel mode
 */
void rc2014Network::rc2014_set_channel_mode()
{
    rc2014_send_ack();

    switch (cmdFrame.aux2)
    {
    case 0:
        channelMode = PROTOCOL;
        rc2014_send_complete();
        break;
    case 1:
        channelMode = JSON;
        rc2014_send_complete();
        break;
    default:
        rc2014_send_error();
    }
}


/**
 * Get Prefix
 */
void rc2014Network::get_prefix()
{
    rc2014_send_ack();

    Debug_printf("rc2014Network::rc2014_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();

    rc2014_send_complete();
}

/**
 * Set Prefix
 */
void rc2014Network::set_prefix(unsigned short s)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));

    rc2014_recv_buffer(prefixSpec, s);
    rc2014_send_ack();

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("rc2014Network::rc2014_set_prefix(%s)\n", prefixSpec_str.c_str());

    if (prefixSpec_str == "..") // Devance path N:..
    {
        std::vector<int> pathLocations;
        for (int i = 0; i < prefix.size(); i++)
        {
            if (prefix[i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (prefix[prefix.size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        prefix = prefix.substr(0, pathLocations.back() + 1);
    }
    else if (prefixSpec_str[0] == '/') // N:/DIR
    {
        prefix = prefixSpec_str;
    }
    else if (prefixSpec_str.empty())
    {
        prefix.clear();
    }
    else if (prefixSpec_str.find_first_of(":") != string::npos)
    {
        prefix = prefixSpec_str;
    }
    else // append to path.
    {
        prefix += prefixSpec_str;
    }

    Debug_printf("Prefix now: %s\n", prefix.c_str());
    rc2014_send_complete();
}

/**
 * Set login
 */
void rc2014Network::set_login()
{
    uint8_t loginspec[USERNAME_BUFFER_SIZE];
    uint16_t num_bytes = cmdFrame.aux1;

    Debug_printf("rc2014Network::set_login()\n");

    rc2014_send_ack();
    memset(loginspec, 0, sizeof(loginspec));

    if (num_bytes > 0) {
        auto rlen = rc2014_recv_buffer(loginspec, num_bytes);
        Debug_printf("rc2014Network::set_login read %d (of %d)\n", rlen, num_bytes);
        rc2014_send_ack();

        login = string((char *)loginspec, num_bytes);
        Debug_printf("rc2014Network::set_login %s\n", login.c_str());
    } else {
        login = "";
        Debug_printf("rc2014Network::set_login emptied\n");
    }
    rc2014_send_complete();
}

/**
 * Set password
 */
void rc2014Network::set_password()
{
    uint8_t passwordspec[PASSWORD_BUFFER_SIZE];
    uint16_t num_bytes = cmdFrame.aux1;

    Debug_printf("rc2014Network::set_password()\n");
    rc2014_send_ack();
    memset(passwordspec, 0, sizeof(passwordspec));

    if (num_bytes > 0) {
        rc2014_recv_buffer(passwordspec, num_bytes);
        rc2014_send_ack();

        password = string((char *)passwordspec, num_bytes);
        Debug_printf("rc2014Network::set_password %s\n", password.c_str());
    } else {
        password = "";
        Debug_printf("rc2014Network::set_login emptied\n");
    }
    rc2014_send_complete();
}


/**
 * Process incoming rc2014 command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void rc2014Network::rc2014_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

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
    case 'P':
        if (channelMode == JSON)
            rc2014_parse_json();
        break;
    case 'Q':
        if (channelMode == JSON)
            rc2014_set_json_query();
        break;
    case 'S':
        status();
        break;
    case 0xFC:
        rc2014_set_channel_mode();
        break;
    case 0xFD:
        set_login();
        break;
    case 0xFE:
        set_password();
        break;
    default:
        Debug_printf("rc2014 network: unimplemented command: 0x%02x", cmdFrame.comnd);

    }
}

bool rc2014Network::rc2014_poll_interrupt()
{
    NetworkStatus s;
    bool result = false;

    if (protocol != nullptr)
    {
        if (protocol->interruptEnable == false)
            return false;

        protocol->fromInterrupt = true;

        switch (channelMode)
        {
        case PROTOCOL:
            err = protocol->status(&s);
            break;
        case JSON:
            err = status_channel_json(&s);
            break;
        }

        protocol->fromInterrupt = false;

        if (s.rxBytesWaiting > 0 || s.connected == 0)
            result = true;
    }

    return result;
}


/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool rc2014Network::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }
    
    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("rc2014Network::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("rc2014Network::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void rc2014Network::create_devicespec(string d)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, cmdFrame.aux1 == 6, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void rc2014Network::create_url_parser()
{
    std::string url = deviceSpec;
    urlParser = PeoplesUrlParser::parseURL(url);
}

void rc2014Network::parse_and_instantiate_protocol(string d)
{
    create_devicespec(d);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: >%s<\n", deviceSpec.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_INVALID_DEVICESPEC;
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", deviceSpec.c_str(), urlParser->mRawUrl.c_str());
        statusByte.byte = 0x00;
        statusByte.bits.client_error = true;
        err = NETWORK_ERROR_GENERAL;
        return;
    }
}

void rc2014Network::set_translation()
{
    // trans_aux2 = cmdFrame.aux2;
    // rc2014_complete();
}

void rc2014Network::set_timer_rate()
{
    // timerRate = (cmdFrame.aux2 * 256) + cmdFrame.aux1;

    // // Stop extant timer
    // timer_stop();

    // // Restart timer if we're running a protocol.
    // if (protocol != nullptr)
    //     timer_start();

    // rc2014_complete();
}


#endif /* NEW_TARGET */
