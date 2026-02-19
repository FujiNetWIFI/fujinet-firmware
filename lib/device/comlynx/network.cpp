#ifdef BUILD_LYNX

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"

#include <cstring>
#include <algorithm>

#include "../../include/debug.h"

#include "utils.h"

#include "status_error_codes.h"
#include "ProtocolParser.h"

using namespace std;

/**
 * Constructor
 */
lynxNetwork::lynxNetwork()
{
    receiveBuffer = new string();
    transmitBuffer = new string();
    specialBuffer = new string();

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    json.setLineEnding("\x00");
}

/**
 * Destructor
 */
lynxNetwork::~lynxNetwork()
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

/** LYNX COMMANDS ***************************************************************/

/**
 * LYNX Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void lynxNetwork::open(unsigned short len)
{
    uint8_t _aux1;
    uint8_t _aux2;
    string d;


    transaction_get(&_aux1, sizeof(_aux1));
    transaction_get(&_aux2, sizeof(_aux2));

    memset(response, 0, sizeof(response));
    transaction_get(&response, len);

    channelMode = PROTOCOL;

    // persist aux1/aux2 values
    cmdFrame.aux1 = _aux1;
    cmdFrame.aux2 = _aux2;

    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;

    // Shut down protocol if we are sending another open before we close.
    if (protocol != nullptr)
    {
        protocol->close();
        delete protocol;
        protocol = nullptr;

        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
    }

    // Reset status buffer
    statusByte.byte = 0x00;

    Debug_printf("lynxNetwork::open()\n");

    // Parse and instantiate protocol
    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr)
    {
        transaction_error();
        return;
    }

    // Attempt protocol open
    if (protocol->open(urlParser.get(), &cmdFrame) == true)
    {
        statusByte.bits.client_error = true;
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        delete protocol;
        protocol = nullptr;
        if (protocolParser != nullptr)
        {
            delete protocolParser;
            protocolParser = nullptr;
        }
        transaction_error();
        return;
    }

    // Associate channel mode
    json.setProtocol(protocol);
    transaction_complete();
}

/**
 * LYNX Close command
 * Tear down everything set up by open(), as well as RX interrupt.
 */
void lynxNetwork::close()
{
    Debug_printf("lynxNetwork::close()\n");

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }

    comlynx_response_ack();

    statusByte.byte = 0x00;

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
}

/**
 * Perform the channel read based on the channelMode
 * @param num_bytes - number of bytes to read from channel.
 * @return TRUE on error, FALSE on success. Passed directly to bus_to_computer().
 */
bool lynxNetwork::read_channel(unsigned short num_bytes)
{
    bool _err = false;

    switch (channelMode)
    {
    case PROTOCOL:
        _err = protocol->read(num_bytes);
        break;
    case JSON:
        Debug_printf("JSON Not Handled.\n");
        _err = true;
        break;
    }
    return _err;
}

/**
 * LYNX Write command
 * Write # of bytes specified by aux1/aux2 from tx_buffer out to LYNX. If protocol is unable to return requested
 * number of bytes, return ERROR.
 */
void lynxNetwork::write(uint16_t num_bytes)
{
    Debug_printf("lynxNetwork::write\n");
    memset(response, 0, sizeof(response));

    comlynx_recv_buffer(response, num_bytes);

    // Get packet checksum
    if (!comlynx_recv_ck()) {
        comlynx_response_nack();
        return;
    }
    comlynx_response_ack();

    *transmitBuffer += string((char *)response, num_bytes);
    err = comlynx_write_channel(num_bytes);
}


/**
 * Perform the correct write based on value of channelMode
 * @param num_bytes Number of bytes to write.
 * @return TRUE on error, FALSE on success. Used to emit comlynx_error or comlynx_complete().
 */
bool lynxNetwork::comlynx_write_channel(unsigned short num_bytes)
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
 * LYNX Status Command. First try to populate NetworkStatus object from protocol. If protocol not instantiated,
 * or Protocol does not want to fill status buffer (e.g. due to unknown aux1/aux2 values), then try to deal
 * with them locally. Then serialize resulting NetworkStatus object to LYNX.
 */
void lynxNetwork::status()
{
    NetworkStatus s;
    NDeviceStatus *status = (NDeviceStatus *) response;

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
        // err = json.status(&status);
        break;
    }

    size_t avail = protocol->available();
    avail = avail > 65535 ? 65535 : avail;
    status->avail = avail;
    status->conn = s.connected;
    status->err = s.error;
    response_len = sizeof(*status);
    receiveMode = STATUS;

    transaction_put(response, response_len);
}

/**
 * Get Prefix
 */
void lynxNetwork::get_prefix()
{
    Debug_printf("lynxNetwork::comlynx_getprefix(%s)\n", prefix.c_str());
    memcpy(response, prefix.data(), prefix.size());
    response_len = prefix.size();

    transaction_put(response, response_len);
}

/**
 * Set Prefix
 */
void lynxNetwork::set_prefix(unsigned short len)
{
    uint8_t prefixSpec[256];
    string prefixSpec_str;

    memset(prefixSpec, 0, sizeof(prefixSpec));
    transaction_get(&prefixSpec, len);

    prefixSpec_str = string((const char *)prefixSpec);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("lynxNetwork::comlynx_set_prefix(%s)\n", prefixSpec_str.c_str());

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
    transaction_complete();
}

/**
 * Set login
 */
void lynxNetwork::set_login(uint16_t len)
{
    uint8_t loginspec[256];

    memset(loginspec, 0, sizeof(loginspec));
    transaction_get(loginspec, len);

    login = string((char *)loginspec, len);
    transaction_complete();
}

/**
 * Set password
 */
void lynxNetwork::set_password(uint16_t len)
{
    uint8_t passwordspec[256];

    memset(passwordspec, 0, sizeof(passwordspec));
    transaction_get(passwordspec, len);

    password = string((char *)passwordspec, len);
    transaction_complete();
}

void lynxNetwork::del(uint16_t len)
{
    string d;

    memset(response, 0, sizeof(response));
    //comlynx_recv_buffer(response, s);
    transaction_get(response, len);

    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    if (protocol == nullptr) {
        transaction_error();
        return;
    }

    cmdFrame.comnd = '!';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    transaction_complete();
}

void lynxNetwork::rename(uint16_t len)
{
    string d;

    memset(response, 0, sizeof(response));
    //comlynx_recv_buffer(response, s);
    transaction_get(response, len);

    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = ' ';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }

    transaction_complete();
}

void lynxNetwork::mkdir(uint16_t len)
{
    string d;

    memset(response, 0, sizeof(response));
    transaction_get(response, len);

    d = string((char *)response, len);
    parse_and_instantiate_protocol(d);

    cmdFrame.comnd = '*';

    if (protocol->perform_idempotent_80(urlParser.get(), &cmdFrame))
    {
        statusByte.bits.client_error = true;
        transaction_error();
        return;
    }
    transaction_complete();
}

void lynxNetwork::channel_mode()
{
    unsigned char m;

    transaction_get(&m, sizeof(m));
    switch (m)
    {
    case 0:
        channelMode = PROTOCOL;
        transaction_complete();
        break;
    case 1:
        channelMode = JSON;
        transaction_complete();
        break;
    default:
        transaction_error();
        break;
    }

    Debug_printf("lynxNetwork::channel_mode - mode:%u\n", m);
}

void lynxNetwork::json_query(unsigned short len)
{
    uint8_t in[256];

    memset(in, 0, sizeof(in));
    transaction_get(in, len);

    // strip away line endings from input spec.
    for (int i = 0; i < len; i++)
    {
        if (in[i] == 0x0A || in[i] == 0x0D || in[i] == 0x9b)
            in[i] = 0x00;
    }

    std::string in_string(reinterpret_cast<char*>(in));
    size_t last_colon_pos = in_string.rfind(':');

    std::string inp_string;
    if (last_colon_pos != std::string::npos) {
        // Skip the device spec. There was a debug message here,
        // but it was removed, because there are cases where
        // removing the devicespec isn't possible, e.g. accessing
        // via CIO (as an XIO). -thom
        inp_string = in_string.substr(last_colon_pos + 1);
    } else {
        inp_string = in_string;
    }

    json.setReadQuery(inp_string, cmdFrame.aux2);

    Debug_printf("lynxNetwork::json_query - (%s)\n", inp_string.c_str());
    transaction_put(response, response_len);
}

void lynxNetwork::json_parse()
{
    Debug_println("lynxNetwork::json_parse");
    json.parse();
    transaction_complete();
}

/**
 * @brief Do an inquiry to determine whether a protoocol supports a particular command.
 * The protocol will either return $00 - No Payload, $40 - Atari Read, $80 - Atari Write,
 * or $FF - Command not supported, which should then be used as a DSTATS value by the
 * Atari when making the N: LYNX call.
 */
void lynxNetwork::comlynx_special_inquiry()
{
}

void lynxNetwork::do_inquiry(fujiCommandID_t inq_cmd)
{
    // Reset inq_dstats
    inq_dstats = SIO_DIRECTION_INVALID;

    cmdFrame.comnd = inq_cmd;

    // Ask protocol for dstats, otherwise get it locally.
    if (protocol != nullptr)
        inq_dstats = protocol->special_inquiry(inq_cmd);

    // If we didn't get one from protocol, or unsupported, see if supported globally.
    if (inq_dstats == 0xFF)
    {
        switch (inq_cmd)
        {
        case FUJICMD_RENAME:
        case FUJICMD_DELETE:
        case FUJICMD_LOCK:
        case FUJICMD_UNLOCK:
        case FUJICMD_MKDIR:
        case FUJICMD_RMDIR:
        case FUJICMD_CHDIR:
        case FUJICMD_USERNAME:
        case FUJICMD_PASSWORD:
            inq_dstats = SIO_DIRECTION_WRITE;
            break;
        case FUJICMD_GETCWD:
            inq_dstats = SIO_DIRECTION_READ;
            break;
        case FUJICMD_TIMER: // Set interrupt rate
            inq_dstats = SIO_DIRECTION_NONE;
            break;
        case FUJICMD_TRANSLATION: // Set Translation
            inq_dstats = SIO_DIRECTION_NONE;
            break;
        case FUJICMD_JSON_PARSE: // JSON Parse
            inq_dstats = SIO_DIRECTION_NONE;
            break;
        case FUJICMD_JSON_QUERY: // JSON Query
            inq_dstats = SIO_DIRECTION_WRITE;
            break;
        default:
            inq_dstats = SIO_DIRECTION_INVALID; // not supported
            break;
        }
    }

    Debug_printf("inq_dstats = %u\n", inq_dstats);
}

/**
 * @brief called to handle special protocol interactions when DSTATS=$00, meaning there is no payload.
 * Essentially, call the protocol action
 * and based on the return, signal comlynx_complete() or error().
 */
void lynxNetwork::comlynx_special_00(unsigned short len)
{
    transaction_get(&cmdFrame.aux1, sizeof(cmdFrame.aux1));
    transaction_get(&cmdFrame.aux2, sizeof(cmdFrame.aux2));

    if (protocol->special_00(&cmdFrame) == false)
        transaction_complete();
    else
        transaction_error();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$40, meaning the payload is to go from
 * the peripheral back to the ATARI. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_computer() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void lynxNetwork::comlynx_special_40(unsigned short len)
{
    transaction_get(&cmdFrame.aux1, sizeof(cmdFrame.aux1));
    transaction_get(&cmdFrame.aux2, sizeof(cmdFrame.aux2));

    if (protocol->special_40(response, 1024, &cmdFrame) == false)
        //transaction_put(response, response_len);
        comlynx_control_receive_channel();
    else
        transaction_error();
}

/**
 * @brief called to handle protocol interactions when DSTATS=$80, meaning the payload is to go from
 * the ATARI to the pheripheral. Essentially, call the protocol action with the accrued special
 * buffer (containing the devicespec) and based on the return, use bus_to_peripheral() to transfer the
 * resulting data. Currently this is assumed to be a fixed 256 byte buffer.
 */
void lynxNetwork::comlynx_special_80(unsigned short len)
{
    uint8_t spData[SPECIAL_BUFFER_SIZE];

    memset(spData, 0, SPECIAL_BUFFER_SIZE);

    // Get special (devicespec) from computer
    transaction_get(&cmdFrame.aux1, sizeof(cmdFrame.aux1));
    transaction_get(&cmdFrame.aux1, sizeof(cmdFrame.aux1));
    transaction_get(spData, len);

    Debug_printf("lynxNetwork::comlynx_special_80() - %s\n", spData);

    // Do protocol action and return
    if (protocol->special_80(spData, SPECIAL_BUFFER_SIZE, &cmdFrame) == false)
        //transaction_put(response, response_len);
        comlynx_control_receive_channel();
    else
        transaction_error();
}

/*
void lynxNetwork::comlynx_control_clr()
{
    comlynx_response_send();

    if (channelMode == JSON)
        jsonRecvd = false;
}
*/

void lynxNetwork::comlynx_control_receive_channel()
{
    switch (channelMode)
    {
    case JSON:
        comlynx_control_receive_channel_json();
        break;
    case PROTOCOL:
        comlynx_control_receive_channel_protocol();
        break;
    }
}

void lynxNetwork::comlynx_control_receive_channel_json()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    if (jsonRecvd == false)
    {
        response_len = json.readValueLen();
        json.readValue(response, response_len);

        Debug_printf("lynxNetwork:receive_channel_json, len:%d %s\n",response_len, response);

        jsonRecvd = true;
        //comlynx_response_ack();
        //transaction_complete();
        transaction_put(response, response_len);
    }
    else
    {
        if (response_len > 0)
            transaction_complete();
        else
            transaction_error();
    }
}

void lynxNetwork::comlynx_control_receive_channel_protocol()
{
    NetworkStatus ns;

    if ((protocol == nullptr) || (receiveBuffer == nullptr))
        return; // Punch out.

    // Get status
    protocol->status(&ns);
    size_t avail = protocol->available();

    if (!avail)
    {
        comlynx_response_nack();
        return;
    }
    /*else
    {
        comlynx_response_ack();
    }*/

    // Truncate bytes waiting to response size
    avail = avail > 1024 ? 1024 : avail;
    response_len = avail;

    if (protocol->read(response_len)) // protocol adapter returned error
    {
        statusByte.bits.client_error = true;
        err = protocol->error;
        transaction_error();
        return;
    }
    else // everything ok
    {
        statusByte.bits.client_error = 0;
        statusByte.bits.client_data_available = response_len > 0;
        memcpy(response, receiveBuffer->data(), response_len);
        receiveBuffer->erase(0, response_len);
        transaction_put(response, response_len);
    }
}

/*
void lynxNetwork::comlynx_control_receive()
{
     // Data is waiting, go ahead and send it off.
    if (response_len > 0)
    {
        comlynx_response_ack();
        return;
    }

    switch (receiveMode)
    {
    case CHANNEL:
        comlynx_control_receive_channel();
        break;
    case STATUS:
        break;
    }
}
*/

/*void lynxNetwork::comlynx_response_send()
{
    //uint8_t c = comlynx_checksum(response, response_len);

    //comlynx_send(0xB0 | _devnum);
    //comlynx_send((NM_SEND << 4) | _devnum);
    //comlynx_send_length(response_len);
    //comlynx_send_buffer(response, response_len);
    //comlynx_send(c);

    transaction_put(response, response_len);

    // print response we're sending
    response[response_len] = '\0';
    Debug_printf("lynxNetwork::comlynx_response_send: %s\n",response);

    // clear response for next time
    memset(response, 0, response_len);
    response_len = 0;
}*/

/**
 * Process incoming LYNX command
 * @param comanddata incoming 4 bytes containing command and aux bytes
 * @param checksum 8 bit checksum
 */
void lynxNetwork::comlynx_process()
{
    fujiCommandID_t c;

   
    // Get the entire payload from Lynx
    uint16_t len = comlynx_recv_length();
    Debug_printf("lynxNetwork::comlynx_process - len: %ld, ", len);

    comlynx_recv_buffer(recvbuffer, len);
    if (comlynx_recv_ck()) {
        Debug_printf("checksum good\n");
        comlynx_response_ack();        // good checksum
    }
    else {
        Debug_printf(" checksum bad\n");
        comlynx_response_nack();       // good checksum
        return;
    }

    // get command
    transaction_get(&c, sizeof(c));
    Debug_printf("lynxNetwork::comlynx_process - command: %02X\n", c);
    len--;      // we received command already
    
   //uint16_t s = comlynx_recv_length(); // receive length
   //fujiCommandID_t c = (fujiCommandID_t) comlynx_recv();         // receive command
    //s--; // Because we've popped the command off the stack

    switch (c)
    {
    case FUJICMD_RENAME:
        rename(len);
        break;
    case FUJICMD_DELETE:
        del(len);
        break;
    case FUJICMD_MKDIR:
        mkdir(len);
        break;
    case FUJICMD_CHDIR:
        set_prefix(len);
        break;
    case FUJICMD_GETCWD:
        get_prefix();
        break;
    case FUJICMD_OPEN:
        open(len);
        break;
    case FUJICMD_CLOSE:
        close();
        break;
    case FUJICMD_STATUS:
        status();
        break;
    case FUJICMD_WRITE:
        write(len);
        break;
    case FUJICMD_JSON:
        channel_mode();
        break;
    case FUJICMD_USERNAME: // login
        set_login(len);
        break;
    case FUJICMD_PASSWORD: // password
        set_password(len);
        break;
    default:
        switch (channelMode)
        {
        case PROTOCOL:
            if (inq_dstats == SIO_DIRECTION_NONE)
                comlynx_special_00(len);
            else if (inq_dstats == SIO_DIRECTION_READ)
                comlynx_special_40(len);
            else if (inq_dstats == SIO_DIRECTION_WRITE)
                comlynx_special_80(len);
            else
                Debug_printf("lynxNetwork::comlynx_process - unknown command: %02x\n", c);
            break;
        case JSON:
            switch (c)
            {
            case FUJICMD_PUT:
                json_parse();
                break;
            case FUJICMD_QUERY:
                json_query(len);
                break;
            default:
                break;
            }
            break;
        default:
            Debug_println("lynxNetwork::comlynx_process - unknown channel mode");
            break;
        }
        do_inquiry(c);
    }
}

/** PRIVATE METHODS ************************************************************/

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open
 */
bool lynxNetwork::instantiate_protocol()
{
    if (!protocolParser)
    {
        protocolParser = new ProtocolParser();
    }

    protocol = protocolParser->createProtocol(urlParser->scheme, receiveBuffer, transmitBuffer, specialBuffer, &login, &password);

    if (protocol == nullptr)
    {
        Debug_printf("lynxNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("lynxNetwork::instantiate_protocol() - Protocol %s created.\n", urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void lynxNetwork::create_devicespec(string d)
{
    deviceSpec = util_devicespec_fix_for_parsing(d, prefix, cmdFrame.aux1 == 6, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void lynxNetwork::create_url_parser()
{
    std::string url = deviceSpec.substr(deviceSpec.find(":") + 1);
    urlParser = PeoplesUrlParser::parseURL(url);
}

void lynxNetwork::parse_and_instantiate_protocol(string d)
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

void lynxNetwork::comlynx_set_translation()
{
}

void lynxNetwork::comlynx_set_timer_rate()
{
}

void lynxNetwork::transaction_complete()
{
    Debug_println("transaction_complete - sent ACK");
    comlynx_response_ack();
}

void lynxNetwork::transaction_error()
{
    Debug_println("transaction_error - send NAK");
    comlynx_response_nack();
    
    // throw away any waiting bytes
    while (SYSTEM_BUS.available() > 0)
        SYSTEM_BUS.read();
}
    
bool lynxNetwork::transaction_get(void *data, size_t len) 
{
    size_t remaining = recvbuffer_len - (recvbuf_pos - recvbuffer);
    size_t to_copy = (len > remaining) ? remaining : len;

    memcpy(data, recvbuf_pos, to_copy);
    recvbuf_pos += to_copy;

    return len;
}

void lynxNetwork::transaction_put(const void *data, size_t len, bool err)
{
    uint8_t b;

    // set response buffer
    memcpy(response, data, len);
    response_len = len;

    // send all data back to Lynx
    uint8_t ck = comlynx_checksum(response, response_len);
    comlynx_send_length(response_len);
    comlynx_send_buffer(response, response_len);
    comlynx_send(ck);

    // get ACK or NACK from Lynx, we're ignoring currently
    //uint8_t t = comlynx_recv_timeout(&b, 8000);
    uint8_t r = comlynx_recv();
    #ifdef DEBUG
        //if (!t)
            if (r == FUJICMD_ACK)
                Debug_println("transaction_put - Lynx ACKed");
            else
                Debug_println("transaction put - Lynx NAKed");
        //else
        //    Debug_println("transaction_put - timed out waiting for ACK/NAK from Lynx");
    #endif

    return;
}

#endif /* BUILD_LYNX */
