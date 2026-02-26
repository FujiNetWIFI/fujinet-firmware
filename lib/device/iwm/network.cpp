#ifdef BUILD_APPLE

/**
 * N: Firmware
 */

#include "network.h"
#include "../network.h"

#include <cstring>
#include <ctype.h>
#include <algorithm>

#include "../../include/debug.h"
#include "../../hardware/led.h"

#include "utils.h"
#include "string_utils.h"

#include "status_error_codes.h"
#include "NetworkProtocolFactory.h"

using namespace std;

/**
 * Constructor
 */
iwmNetwork::iwmNetwork()
{
    Debug_printf("iwmNetwork::iwmNetwork()\n");
}

/**
 * Destructor
 */
iwmNetwork::~iwmNetwork()
{
}

/** iwm COMMANDS ***************************************************************/

void iwmNetwork::shutdown()
{
    // TODO: come back here and make shutdown() close all connections.
}

void iwmNetwork::send_status_reply_packet()
{
    uint8_t data[4];

    // Build the contents of the packet
    data[0] = STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
    data[1] = 0; // block size 1
    data[2] = 0; // block size 2
    data[3] = 0; // block size 3
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data, 4);
}

void iwmNetwork::send_status_dib_reply_packet()
{
    Debug_printf("\r\nNETWORK: Sending DIB reply\r\n");
    std::vector<uint8_t> data = create_dib_reply_packet(
        "NETWORK",                                                          // name
        STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE,                     // status
        { 0, 0, 0 },                                                        // block size
        { SP_TYPE_BYTE_FUJINET_NETWORK, SP_SUBTYPE_BYTE_FUJINET_NETWORK },  // type, subtype
        { 0x00, 0x01 }                                                      // version.
    );
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::status, SP_ERR_NOERROR, data.data(), data.size());
}

/**
 * net Open command
 * Called in response to 'O' command. Instantiate a protocol, pass URL to it, call its open
 * method. Also set up RX interrupt.
 */
void iwmNetwork::open()
{
    // This will create the entry if one doesn't exist yet.
    auto& current_network_data = network_data_map[current_network_unit];
    uint8_t _aux1 = data_buffer[0];
    uint8_t _aux2 = data_buffer[1];

    auto start = data_buffer + 2;
    auto end = start + std::min<std::size_t>(256, sizeof(data_buffer) - 2);
    auto null_pos = std::find(start, end, 0);

    // ensure the string does not go past a null, but can be up to 256 bytes long if one not found
    string d(start, null_pos);

    Debug_printf("\naux1: %u aux2: %u path %s", _aux1, _aux2, d.c_str());

    current_network_data.channelMode = CHANNEL_MODE::PROTOCOL;

    // persist aux1/aux2 values - this is a smell
    cmdFrame.aux1 = _aux1;
    cmdFrame.aux2 = _aux2;

    open_aux1 = cmdFrame.aux1;
    open_aux2 = cmdFrame.aux2;

    // Shut down protocol if we are sending another open before we close.
    if (current_network_data.protocol)
    {
        current_network_data.protocol->close();
        // manually force the memory out
        current_network_data.protocol.reset();
        current_network_data.json.reset();
        current_network_data.urlParser.reset();
    }

    err = SP_ERR_NOERROR;

    Debug_printf("\nopen()\n");

    // Parse and instantiate protocol
    parse_and_instantiate_protocol(d);

    if (!current_network_data.protocol)
    {
        return;
    }

    // Attempt protocol open
    if (current_network_data.protocol->open(current_network_data.urlParser.get(), (fileAccessMode_t) cmdFrame.aux1, (netProtoTranslation_t) cmdFrame.aux2) != PROTOCOL_ERROR::NONE)
    {
        Debug_printf("Protocol unable to make connection. Error: %d\n", err);
        current_network_data.protocol.reset();
        return;
    }

    // Associate channel mode
    current_network_data.json = std::make_unique<FNJSON>();
    current_network_data.json->setProtocol(current_network_data.protocol.get());
    current_network_data.json->setLineEnding("\x0a");
}

/**
 * iwm Close command
 */
void iwmNetwork::close()
{
    Debug_printf("iwmNetwork::close()\n");
    err = SP_ERR_NOERROR;
    if (network_data_map.find(current_network_unit) == network_data_map.end()) {
        Debug_printf("No network_data for unit: %d, exiting close\r\n", current_network_unit);
        return;
    }

    auto& current_network_data = network_data_map[current_network_unit];

    // close before doing anything to the buffers
    if (current_network_data.protocol) current_network_data.protocol->close();

    // belt and bracers! the erase from the map will delete the object and clean up any managed data, including strings.
    current_network_data.receiveBuffer.clear();
    current_network_data.transmitBuffer.clear();
    current_network_data.specialBuffer.clear();


    // technically not required as removing the item from the map will also remove the value
    if (current_network_data.protocol) current_network_data.protocol.reset();
    if (current_network_data.json) current_network_data.json.reset();
    if (current_network_data.urlParser) current_network_data.urlParser.reset();

    network_data_map.erase(current_network_unit);
}

/**
 * Get Prefix
 */
void iwmNetwork::get_prefix()
{
    auto& current_network_data = network_data_map[current_network_unit];

    Debug_printf("iwmNetwork::get_prefix(%s)\n", current_network_data.prefix.c_str());
    memset(data_buffer, 0, sizeof(data_buffer));
    memcpy(data_buffer, current_network_data.prefix.c_str(), current_network_data.prefix.length());
    data_len = current_network_data.prefix.length();
}

/**
 * Set Prefix
 */
void iwmNetwork::set_prefix()
{
    auto& current_network_data = network_data_map[current_network_unit];

    string prefixSpec_str = string((const char *)data_buffer);
    prefixSpec_str = prefixSpec_str.substr(prefixSpec_str.find_first_of(":") + 1);
    Debug_printf("iwmNetwork::iwmnet_set_prefix(%s)\n", prefixSpec_str.c_str());

    if (prefixSpec_str == "..") // Devance path N:..
    {
        std::vector<int> pathLocations;
        for (int i = 0; i < current_network_data.prefix.size(); i++)
        {
            if (current_network_data.prefix[i] == '/')
            {
                pathLocations.push_back(i);
            }
        }

        if (current_network_data.prefix[current_network_data.prefix.size() - 1] == '/')
        {
            // Get rid of last path segment.
            pathLocations.pop_back();
        }

        // truncate to that location.
        current_network_data.prefix = current_network_data.prefix.substr(0, pathLocations.back() + 1);
    }
    else if (prefixSpec_str[0] == '/') // N:/DIR
    {
        current_network_data.prefix = prefixSpec_str;
    }
    else if (prefixSpec_str.empty())
    {
        current_network_data.prefix.clear();
    }
    else if (prefixSpec_str.find_first_of(":") != string::npos)
    {
        current_network_data.prefix = prefixSpec_str;
    }
    else // append to path.
    {
        current_network_data.prefix += prefixSpec_str;
    }

    Debug_printf("Prefix now: %s\n", current_network_data.prefix.c_str());
}

/**
 * Set login
 */
void iwmNetwork::set_login()
{
    auto& current_network_data = network_data_map[current_network_unit];

    current_network_data.login = string((char *)data_buffer, 256);
    Debug_printf("Login is %s\n", current_network_data.login.c_str());
}

/**
 * Set password
 */
void iwmNetwork::set_password()
{
    auto& current_network_data = network_data_map[current_network_unit];

    current_network_data.password = string((char *)data_buffer, 256);
    Debug_printf("Password is %s\n", current_network_data.password.c_str()); // GREAT LOGGING
}

void iwmNetwork::channel_mode()
{
    auto& current_network_data = network_data_map[current_network_unit];
    switch (data_buffer[0])
    {
    case 0:
        Debug_printf("channelMode = PROTOCOL\n");
        current_network_data.channelMode = CHANNEL_MODE::PROTOCOL;
        break;
    case 1:
        Debug_printf("channelMode = JSON\n");
        current_network_data.channelMode = CHANNEL_MODE::JSON;
        break;
    default:
        Debug_printf("INVALID MODE = %02x\r\n", data_buffer[0]);
        break;
    }
}

void iwmNetwork::json_query(iwm_decoded_cmd_t cmd)
{
    auto& current_network_data = network_data_map[current_network_unit];
    Debug_printf("\r\nQuery set to: %s, data_len: %d\r\n", string((char *)data_buffer, data_len).c_str(), data_len);
    current_network_data.json->setReadQuery(string((char *)data_buffer, data_len), cmdFrame.aux2);
}

void iwmNetwork::json_parse()
{
    auto& current_network_data = network_data_map[current_network_unit];
    current_network_data.json->parse();
}

void iwmNetwork::iwm_open(iwm_decoded_cmd_t cmd)
{
    // nothing in fujinet-lib calls this, it does a control with open command, so something else called it, let's ensure the default N device is used
    current_network_unit = 1;
    send_reply_packet(SP_ERR_NOERROR);
}

void iwmNetwork::iwm_close(iwm_decoded_cmd_t cmd)
{
    // nothing in fujinet-lib calls this, it does a control with close command, so something else called it, let's ensure the default N device is used
    current_network_unit = 1;
    send_reply_packet(SP_ERR_NOERROR);
    close();
}

void iwmNetwork::status()
{
    auto& current_network_data = network_data_map[current_network_unit];
    NetworkStatus s;
    size_t avail = 0;
    NDeviceStatus *status;

    switch (current_network_data.channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        if (!current_network_data.protocol) {
            Debug_printf("ERROR: Calling status on a null protocol.\r\n");
            err = SP_ERR_BADCMD;
            s.error = NDEV_STATUS::INVALID_COMMAND;
        } else {
            err = current_network_data.protocol->status(&s) == PROTOCOL_ERROR::NONE
                ? SP_ERR_NOERROR : SP_ERR_BADCMD;
            avail = current_network_data.protocol->available();
        }
        break;
    case CHANNEL_MODE::JSON:
        err = (current_network_data.json->status(&s) == false) ? SP_ERR_NOERROR : SP_ERR_IOERROR;
        avail = current_network_data.json->available();
        break;
    }

    Debug_printf("Bytes Waiting: 0x%02x, Connected: %u, Error: %u\n", avail, s.connected, s.error);

    avail = std::min((size_t) 512, avail);
    status = (NDeviceStatus *) data_buffer;
    status->avail = avail;
    status->conn = s.connected;
    status->err = s.error;
    data_len = sizeof(*status);
}

void iwmNetwork::iwm_status(iwm_decoded_cmd_t cmd)
{
    uint8_t status_code = get_status_code(cmd); //(cmd.g7byte3 & 0x7f) | ((cmd.grp7msb << 3) & 0x80); // status codes 00-FF

    // fujinet-lib (with unit-id support) sends the count of bytes for a status as 4 to cater for the network unit.
    // Older code sends 3 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.count != 4) {
        current_network_unit = 1;
    } else {
        current_network_unit = cmd.params[3];
    }

#ifdef DEBUG
    char as_char = (char) status_code;
    Debug_printf("\r\n[NETWORK] Device %02x Status Code %02x('%c') net_unit %02x\r\n", id(), status_code, isprint(as_char) ? as_char : '.', current_network_unit);
#endif

    // auto& current_network_data = network_data_map[current_network_unit];

    switch (status_code)
    {
    case IWM_STATUS_STATUS: // 0x00
        send_status_reply_packet();
        return;
        break;
    // case IWM_STATUS_DCB:                  // 0x01
    // case IWM_STATUS_NEWLINE:              // 0x02
    case IWM_STATUS_DIB: // 0x03
        send_status_dib_reply_packet();
        return;
        break;
    case NETCMD_GETCWD:
        get_prefix();
        break;
    case NETCMD_READ:
        net_read();
        break;
    case NETCMD_STATUS:
        status();
        break;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    //send_data_packet(data_len);
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmNetwork::net_read()
{
}

bool iwmNetwork::read_channel_json(unsigned short num_bytes, iwm_decoded_cmd_t cmd)
{
    auto& current_network_data = network_data_map[current_network_unit];
    Debug_printf("read_channel_json - num_bytes: %02x, json_bytes_remaining: %02x\n", num_bytes, current_network_data.json->available());
    if (current_network_data.json->available() == 0) // if no bytes, we just return with no data
    {
        data_len = 0;
    }
    else if (num_bytes > current_network_data.json->available())
    {
        data_len = current_network_data.json->readValueLen();
        current_network_data.json->readValue(data_buffer, data_len);

        // Debug_printf("read_channel_json(1) - data_len: %02x, json_bytes_remaining: %02x\n", data_len, current_network_data.json->json_bytes_remaining);
        // int print_len = data_len;
        // if (print_len > 16) print_len = 16;
        //std::string msg = util_hexdump(data_buffer, print_len);
        //Debug_printf("%s\n", msg.c_str());
        //if (print_len != data_len) {
        //    Debug_printf("... truncated");
        //}
    }
    else
    {
        current_network_data.json->readValue(data_buffer, num_bytes);
        data_len = current_network_data.json->readValueLen();

        // Debug_printf("read_channel_json(2) - data_len: %02x, json_bytes_remaining: %02x\n", num_bytes, current_network_data.json->json_bytes_remaining);
        // int print_len = num_bytes;
        // if (print_len > 16) print_len = 16;
        //std::string msg = util_hexdump(data_buffer, print_len);
        //Debug_printf("%s\n", msg.c_str());
        //if (print_len != num_bytes) {
        //    Debug_printf("... truncated");
        //}
    }

    return false;
}

bool iwmNetwork::read_channel(unsigned short num_bytes, iwm_decoded_cmd_t cmd)
{
    NetworkStatus ns;
    size_t avail;
    auto& current_network_data = network_data_map[current_network_unit];

    if (!current_network_data.protocol)
        return true; // Punch out.

    avail = current_network_data.protocol->available();
    data_len = std::min((size_t) num_bytes, std::min((size_t) 512, avail));

    //Debug_printf("\r\nAvailable bytes %04x\n", data_len);

    if (current_network_data.protocol->read(data_len) != PROTOCOL_ERROR::NONE) // protocol adapter returned error
    {
        err = current_network_data.protocol->error == NDEV_STATUS::SUCCESS ? SP_ERR_NOERROR : SP_ERR_BADCMD;
        return true;
    }
    else // everything ok
    {
        memcpy(data_buffer, current_network_data.receiveBuffer.data(), data_len);
        current_network_data.receiveBuffer.erase(0, data_len);
    }
    return false;
}

bool iwmNetwork::write_channel(unsigned short num_bytes)
{
    auto& current_network_data = network_data_map[current_network_unit];
    switch (current_network_data.channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        current_network_data.protocol->write(num_bytes);
    case CHANNEL_MODE::JSON:
        break;
    }
    return false;
}

void iwmNetwork::iwm_read(iwm_decoded_cmd_t cmd)
{
    bool error = false;
    uint16_t numbytes = get_numbytes(cmd);

    // fujinet-lib (with unit-id support) sends the count of bytes for a read as 5 to cater for the network unit.
    // Older code sends 4 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.count != 5) {
        current_network_unit = 1;
    } else {
        // in a network device, there is no "address" value, this is hijacked by fujinet-lib to pass the network unit in first byte
        current_network_unit = cmd.params[4];
    }

    Debug_printf("\r\nDevice %02x Read %04x bytes, net_unit %02x\n", id(), numbytes, current_network_unit);

    auto& current_network_data = network_data_map[current_network_unit];

    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));

    switch (current_network_data.channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        error = read_channel(numbytes, cmd);
        break;
    case CHANNEL_MODE::JSON:
        error = read_channel_json(numbytes, cmd);
        break;
    }

    if (error)
    {
        iwm_return_ioerror();
    }
    else
    {
        Debug_printf("\r\nsending Network read data packet (%04x bytes)...", data_len);
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0, data_buffer, data_len);
        data_len = 0;
        memset(data_buffer, 0, sizeof(data_buffer));
    }
}

void iwmNetwork::net_write()
{
    auto& current_network_data = network_data_map[current_network_unit];
    // TODO: Handle errors.
    current_network_data.transmitBuffer += string((char *)data_buffer, data_len);
    write_channel(data_len);
}

void iwmNetwork::iwm_write(iwm_decoded_cmd_t cmd)
{
    uint16_t num_bytes = get_numbytes(cmd);

    // fujinet-lib (with unit-id support) sends the count of bytes for a write as 5 to cater for the network unit.
    // Older code sends 4 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.count != 5) {
        current_network_unit = 1;
    } else {
        // in a network device, there is no "address" value, this is hijacked by fujinet-lib to pass the network unit in first byte
        current_network_unit = cmd.params[4];
    }

    Debug_printf("\r\nDevice %02x Write %04x bytes, net_unit %02x\n", id(), num_bytes, current_network_unit);

    auto& current_network_data = network_data_map[current_network_unit];

    // get write data packet, keep trying until no timeout
    SYSTEM_BUS.iwm_decode_data_packet((unsigned char *)data_buffer, data_len);

    if (data_len == -1)
        iwm_return_ioerror();
    else
    {
        current_network_data.transmitBuffer += string((char *)data_buffer, num_bytes);
        if (write_channel(num_bytes))
        {
            send_reply_packet(SP_ERR_IOERROR);
        }
        else
        {
            send_reply_packet(SP_ERR_NOERROR);
        }
    }

    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmNetwork::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    uint8_t err_result = SP_ERR_NOERROR;

    fujiCommandID_t control_code = (fujiCommandID_t) get_status_code(cmd);

    // fujinet-lib (with unit-id support) sends the count of bytes for a control as 4 to cater for the network unit.
    // Older code sends 3 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.count != 4) {
        current_network_unit = 1;
    } else {
        current_network_unit = cmd.params[3];
    }

#ifdef DEBUG
    char as_char = (char) control_code;
    Debug_printf("\r\nNet Device %02x Control Code %02x('%c') net_unit %02x", id(), control_code, isprint(as_char) ? as_char : '.', current_network_unit);
#endif

    auto& current_network_data = network_data_map[current_network_unit];

    SYSTEM_BUS.iwm_decode_data_packet((uint8_t *)data_buffer, data_len);
    print_packet((uint8_t *)data_buffer);

    // Debug_printv("cmd (looking for network_unit in byte 6, i.e. hex[5]):\r\n%s\r\n", mstr::toHex(cmd.decoded, 9).c_str());

    if (control_code != NETCMD_OPEN && current_network_data.json == nullptr) {
        Debug_printv("control should not be called on a non-open channel - FN was probably reset");
    }

    switch (control_code)
    {
    case NETCMD_CHDIR:
        set_prefix();
        break;
    case NETCMD_GETCWD:
        get_prefix();
        break;
    case NETCMD_OPEN:
        open();
        break;
    case NETCMD_CLOSE:
        close();
        break;
    case NETCMD_WRITE:
        net_write();
        break;
    case NETCMD_CHANNEL_MODE:
        channel_mode();
        break;
    case NETCMD_USERNAME: // login
        set_login();
        break;
    case NETCMD_PASSWORD: // password
        set_password();
        break;

    case NETCMD_PARSE:
        json_parse();
        break;
    case NETCMD_QUERY:
        json_query(cmd);
        break;

    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_LOCK:
    case NETCMD_UNLOCK:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        process_fs(control_code);
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp(control_code);
        break;

    case NETCMD_UNLISTEN:
        process_http(control_code);
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp(control_code);
        break;

    default:
        err = SP_ERR_IOERROR;
        break;
    }

    if (err != 0)
        err_result = SP_ERR_IOERROR;

    send_reply_packet(err_result);

    data_len = 0;
    memset(data_buffer, 0, sizeof(data_buffer));
}

void iwmNetwork::process(iwm_decoded_cmd_t cmd)
{
    fnLedManager.set(LED_BUS, true);
    switch (cmd.command)
    {
    case SP_CMD_STATUS:
        Debug_printf("\r\nhandling status command");
        iwm_status(cmd);
        break;
    case SP_CMD_CONTROL:
        Debug_printf("\r\nhandling control command");
        iwm_ctrl(cmd);
        break;
    case SP_CMD_OPEN:
        Debug_printf("\r\nhandling open command");
        iwm_open(cmd);
        break;
    case SP_CMD_CLOSE:
        Debug_printf("\r\nhandling close command");
        iwm_close(cmd);
        break;
    case SP_CMD_READ:
        Debug_printf("\r\nhandling read command");
        iwm_read(cmd);
        break;
    case SP_CMD_WRITE:
        iwm_write(cmd);
        break;
    default:
        iwm_return_badcmd(cmd);
        break;
    } // switch (cmd)
    fnLedManager.set(LED_BUS, false);
}

/**
 * Instantiate protocol object
 * @return bool TRUE if protocol successfully called open(), FALSE if protocol could not open - WHY IS SUCCESS true HERE AND ERROR true MOST OTHER PLACES? :hair_pull:
 */
bool iwmNetwork::instantiate_protocol()
{
    auto& current_network_data = network_data_map[current_network_unit];
    current_network_data.protocol = std::move(NetworkProtocolFactory::createProtocol(current_network_data.urlParser->scheme, current_network_data));

    if (!current_network_data.protocol)
    {
        Debug_printf("iwmNetwork::instantiate_protocol() - Could not create protocol.\n");
        return false;
    }

    Debug_printf("iwmNetwork::instantiate_protocol() - Protocol %s created.\n", current_network_data.urlParser->scheme.c_str());
    return true;
}

/**
 * Preprocess deviceSpec given aux1 open mode. This is used to work around various assumptions that different
 * disk utility packages do when opening a device, such as adding wildcards for directory opens.
 */
void iwmNetwork::create_devicespec(string d)
{
    auto& current_network_data = network_data_map[current_network_unit];
    current_network_data.deviceSpec = util_devicespec_fix_for_parsing(d, current_network_data.prefix, cmdFrame.aux1 == 6, false);
}

/*
 * The resulting URL is then sent into a URL Parser to get our URLParser object which is used in the rest
 * of Network.
*/
void iwmNetwork::create_url_parser()
{
    auto& current_network_data = network_data_map[current_network_unit];
    std::string url = current_network_data.deviceSpec.substr(current_network_data.deviceSpec.find(":") + 1);
    current_network_data.urlParser = std::move(PeoplesUrlParser::parseURL(url));
}

void iwmNetwork::parse_and_instantiate_protocol(string d)
{
    auto& current_network_data = network_data_map[current_network_unit];
    create_devicespec(d);
    create_url_parser();

    // Invalid URL returns error 165 in status.
    if (!current_network_data.urlParser->isValidUrl())
    {
        Debug_printf("Invalid devicespec: %s\n", current_network_data.deviceSpec.c_str());
        err = SP_ERR_BADCTLPARM;
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", current_network_data.deviceSpec.c_str(), current_network_data.urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", current_network_data.deviceSpec.c_str(), current_network_data.urlParser->mRawUrl.c_str());
        err = SP_ERR_BADCMD;
        return;
    }
}

void iwmNetwork::iwmnet_set_translation()
{
}

void iwmNetwork::iwmnet_set_timer_rate()
{
}

void iwmNetwork::process_fs(fujiCommandID_t control_code)
{
    auto& current_network_data = network_data_map[current_network_unit];
    string d;

    d = string((char *)data_buffer, 256);
    parse_and_instantiate_protocol(d);

    if (!current_network_data.protocol)
        return;

    // Make sure this is really a FS protocol instance
    NetworkProtocolFS *fs = dynamic_cast<NetworkProtocolFS *>(current_network_data.protocol.get());
    if (!fs)
    {
        err = SP_ERR_IOERROR;
        return;
    }

    protocolError_t cmd_err;
    auto url = current_network_data.urlParser.get();
    switch (control_code)
    {
    case NETCMD_RENAME:
        cmd_err = fs->rename(url);
        break;
    case NETCMD_DELETE:
        cmd_err = fs->del(url);
        break;
    case NETCMD_LOCK:
        cmd_err = fs->lock(url);
        break;
    case NETCMD_UNLOCK:
        cmd_err = fs->unlock(url);
        break;
    case NETCMD_MKDIR:
        cmd_err = fs->mkdir(url);
        break;
    case NETCMD_RMDIR:
        cmd_err = fs->rmdir(url);
        break;
    default:
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        err = SP_ERR_IOERROR;
}

void iwmNetwork::process_tcp(fujiCommandID_t control_code)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(current_network_data.protocol.get());
    if (!tcp)
    {
        err = SP_ERR_IOERROR;
        return;
    }

    protocolError_t cmd_err;
    switch (control_code)
    {
    case NETCMD_CONTROL:
        cmd_err = tcp->accept_connection();
        break;
    case NETCMD_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        err = SP_ERR_IOERROR;
}

void iwmNetwork::process_http(fujiCommandID_t control_code)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really an HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(current_network_data.protocol.get());
    if (!http)
    {
        err = SP_ERR_IOERROR;
        return;
    }

    protocolError_t cmd_err;
    switch (control_code)
    {
    case NETCMD_UNLISTEN:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) cmdFrame.aux2);
        break;
    default:
        cmd_err = PROTOCOL_ERROR::UNSPECIFIED;
        return;
    }

    if (cmd_err != PROTOCOL_ERROR::NONE)
        err = SP_ERR_IOERROR;
}

void iwmNetwork::process_udp(fujiCommandID_t control_code)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(current_network_data.protocol.get());
    if (!udp)
    {
        err = SP_ERR_IOERROR;
        return;
    }

    protocolError_t cmd_err;
    switch (control_code)
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        cmd_err = udp->get_remote(data_buffer, SPECIAL_BUFFER_SIZE);
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, 0,
                                   data_buffer, SPECIAL_BUFFER_SIZE);
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            cmd_err = udp->set_destination(data_buffer, data_len);
            if (cmd_err != PROTOCOL_ERROR::NONE)
                err = SP_ERR_IOERROR;
        }
        break;
    default:
        err = SP_ERR_IOERROR;
        break;
    }
}

#endif /* BUILD_APPLE */
