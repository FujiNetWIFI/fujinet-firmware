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

iwm_device_status_block_t iwmNetwork::create_status_reply_packet()
{
  iwm_device_status_block_t status;

  status.code = STATCODE_WRITE_ALLOWED | STATCODE_READ_ALLOWED | STATCODE_DEVICE_ONLINE;
  status.block_size = 0;
  return status;
}

iwm_device_info_block_t iwmNetwork::create_dib_reply_packet()
{
  iwm_device_info_block_t dib;

  dib.dev_status = create_status_reply_packet();
  strcpy(dib.name, "NETWORK");
  dib.name_len = strlen(dib.name);
  dib.type = SP_TYPE_BYTE_FUJINET_NETWORK;
  dib.subtype = SP_SUBTYPE_BYTE_FUJINET_NETWORK;
  dib.version = 0x0100;

  return dib;
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

    // Shut down protocol if we are sending another open before we close.
    if (current_network_data.protocol)
    {
        current_network_data.protocol->close();
        // manually force the memory out
        current_network_data.protocol.reset();
        current_network_data.json.reset();
        current_network_data.urlParser.reset();
    }

    err = SP_ERR::NOERROR;
    current_network_data.open_error = NDEV_STATUS::SUCCESS;

    Debug_printf("\nopen()\n");

    // Parse and instantiate protocol
    parse_and_instantiate_protocol(d);

    if (!current_network_data.protocol)
    {
        current_network_data.open_error = NDEV_STATUS::INVALID_DEVICESPEC;
        return;
    }

    // Attempt protocol open
    if (current_network_data.protocol->open(current_network_data.urlParser.get(), (fileAccessMode_t) data_buffer[0], (netProtoTranslation_t) data_buffer[1]) != FUJI_ERROR::NONE)
    {
        // Remember the failure before the protocol (and its error) is destroyed,
        // so a subsequent STATUS can report the real code (e.g. FILE_NOT_FOUND).
        current_network_data.open_error =
            current_network_data.protocol->error != NDEV_STATUS::SUCCESS
                ? current_network_data.protocol->error
                : NDEV_STATUS::GENERAL;
        Debug_printf("Protocol unable to make connection. Error: %d\n",
                     (int)current_network_data.open_error);
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
    err = SP_ERR::NOERROR;
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
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(current_network_data.prefix);
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
    std::string buffer(data_len, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
    buffer.resize(strlen(buffer.c_str())); // Truncate to null terminator
    Debug_printf("\r\nQuery set to: %s, data_len: %d\r\n", buffer.c_str(), buffer.size());
    current_network_data.json->setReadQuery(buffer, 0);
    SYSTEM_BUS.transaction_success();
}

void iwmNetwork::json_parse()
{
    auto& current_network_data = network_data_map[current_network_unit];
    current_network_data.json->parse();
}

void iwmNetwork::iwm_open(iwm_decoded_cmd_t cmd)
{
    // nothing in fujinet-lib calls this, it does a control with open command. This is used only by the Apple/// as it has no fn-lib support yet
    send_reply_packet(SP_ERR::NOERROR);
}

void iwmNetwork::iwm_close(iwm_decoded_cmd_t cmd)
{
    // nothing in fujinet-lib calls this, it does a control with close command. This is used only by the Apple/// as it has no fn-lib support yet
    send_reply_packet(SP_ERR::NOERROR);
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
            err = SP_ERR::BADCMD;
            s.connected = 0;
            // A failed OPEN destroyed the protocol; report the remembered
            // open error (e.g. FILE_NOT_FOUND) rather than a generic code.
            s.error = current_network_data.open_error != NDEV_STATUS::SUCCESS
                        ? current_network_data.open_error
                        : NDEV_STATUS::INVALID_COMMAND;
        } else {
            err = current_network_data.protocol->status(&s) == FUJI_ERROR::NONE
                ? SP_ERR::NOERROR : SP_ERR::BADCMD;
            avail = current_network_data.protocol->available();
        }
        break;
    case CHANNEL_MODE::JSON:
        err = (current_network_data.json->status(&s) == false) ? SP_ERR::NOERROR : SP_ERR::IOERROR;
        avail = current_network_data.json->available();
        break;
    }

    Debug_printf("Bytes Waiting: 0x%02x, Connected: %u, Error: %u\n", avail, s.connected, s.error);

    avail = std::min((size_t) 512, avail);
    status = (NDeviceStatus *) data_buffer;
    status->avail = avail;
    status->conn = s.connected;
    status->err = s.error;
    SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
    SYSTEM_BUS.transaction_send(status, sizeof(*status));
}

void iwmNetwork::iwm_status(iwm_decoded_cmd_t cmd)
{
    // TODO: remove this in the future when we decide to drop support of the deprecated fujinet-lib (with unit-id support)
    // We have moved to a separate control command that sets the active channel for all subsequent commands
    // fujinet-lib (with unit-id support) sends the count of bytes for a status as 4 to cater for the network unit.
    // Older code sends 3 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.param_count == 4) {
        current_network_unit = cmd.control_status.fuji.network_unit;
    }

#ifdef DEBUG
    Debug_printf("\r\n[NETWORK] Device %02x Status Code %02x('%c') net_unit %02x\r\n", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char) cmd.control_status.fuji.command : '.', current_network_unit);
#endif

    switch (cmd.control_status.fuji.command)
    {
    case NETCMD_GETCWD:
        get_prefix();
        break;
    case NETCMD_READ:
        net_read();
        break;
    case NETCMD_STATUS:
        status();
        break;
    default:
        send_reply_packet(SP_ERR::BADCMD);
        return;
    }

    Debug_printf("\r\nStatus code complete, sending response");
    SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
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
    else
    {
        num_bytes = std::min<uint16_t>(num_bytes, current_network_data.json->readValueLen());
        current_network_data.json->readValue(data_buffer, num_bytes);
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(data_buffer, num_bytes);
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
    auto rlen = std::min<size_t>({
            num_bytes, 512, current_network_data.protocol->available()});

    //Debug_printf("\r\nAvailable bytes %04x\n", data_len);

    if (current_network_data.protocol->read(rlen) != FUJI_ERROR::NONE) // protocol adapter returned error
    {
        err = current_network_data.protocol->error == NDEV_STATUS::SUCCESS ? SP_ERR::NOERROR : SP_ERR::BADCMD;
        return true;
    }
    else // everything ok
    {
        SYSTEM_BUS.transaction_accept(TRANS_STATE::NO_GET);
        SYSTEM_BUS.transaction_send(current_network_data.receiveBuffer, rlen);
        current_network_data.receiveBuffer.erase(0, rlen);
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

    // TODO: remove this in the future when we decide to drop support of the deprecated fujinet-lib (with unit-id support)
    // We have moved to a separate control command that sets the active channel for all subsequent commands
    // fujinet-lib (with unit-id support) sends the count of bytes for a read as 5 to cater for the network unit.
    // Older code sends 4 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.param_count == 5) {
        // in a network device, there is no "address" value, this is hijacked by fujinet-lib to pass the network unit in first byte
        current_network_unit = cmd.char_rw.fuji.network_unit;
    }

    Debug_printf("\r\nDevice %02x Read %04x bytes, net_unit %02x\n", id(), cmd.char_rw.length, current_network_unit);

    auto& current_network_data = network_data_map[current_network_unit];

    switch (current_network_data.channelMode)
    {
    case CHANNEL_MODE::PROTOCOL:
        error = read_channel(cmd.char_rw.length, cmd);
        break;
    case CHANNEL_MODE::JSON:
        error = read_channel_json(cmd.char_rw.length, cmd);
        break;
    }

    if (error)
    {
        iwm_return_ioerror();
    }
    else
    {
        Debug_printf("\r\nsending Network read data packet (%04x bytes)...", data_len);
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR, data_buffer, data_len);
    }
}

void iwmNetwork::net_write()
{
    auto& current_network_data = network_data_map[current_network_unit];
    // TODO: Handle errors.
    std::string buffer(data_len, 0);
    SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
    SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
    current_network_data.transmitBuffer += buffer;
    write_channel(buffer.size());
}

void iwmNetwork::iwm_write(iwm_decoded_cmd_t cmd)
{
    // TODO: remove this in the future when we decide to drop support of the deprecated fujinet-lib (with unit-id support)
    // We have moved to a separate control command that sets the active channel for all subsequent commands
    // fujinet-lib (with unit-id support) sends the count of bytes for a write as 5 to cater for the network unit.
    // Older code sends 4 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.param_count == 5) {
        // in a network device, there is no "address" value, this is hijacked by fujinet-lib to pass the network unit in first byte
        current_network_unit = cmd.char_rw.fuji.network_unit;
    }

    Debug_printf("\r\nDevice %02x Write %04x bytes, net_unit %02x\n", id(), cmd.char_rw.length, current_network_unit);

    auto& current_network_data = network_data_map[current_network_unit];

    current_network_data.transmitBuffer += string((char *)data_buffer, cmd.char_rw.length);
    if (write_channel(cmd.char_rw.length))
        send_reply_packet(SP_ERR::IOERROR);
    else
        send_reply_packet(SP_ERR::NOERROR);
}

void iwmNetwork::iwm_ctrl(iwm_decoded_cmd_t cmd)
{
    spError_t err_result = SP_ERR::NOERROR;

    // TODO: remove this in the future when we decide to drop support of the deprecated fujinet-lib (with unit-id support)
    // We have moved to a separate control command that sets the active channel for all subsequent commands
    // fujinet-lib (with unit-id support) sends the count of bytes for a control as 4 to cater for the network unit.
    // Older code sends 3 as the count, so we can detect if the network unit byte is there or not.
    if (cmd.param_count == 4) {
        current_network_unit = cmd.control_status.fuji.network_unit;
    }

    auto& current_network_data = network_data_map[current_network_unit];

    print_packet((uint8_t *)data_buffer);

#ifdef DEBUG
    if (cmd.control_status.fuji.command == NETCMD_SET_CHANNEL)
        Debug_printf("\r\nNet Device %02x Control Code %02x('%c') net_unit %02x", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char)cmd.control_status.fuji.command : '.', data_buffer[0]);
    else
        Debug_printf("\r\nNet Device %02x Control Code %02x('%c') net_unit %02x", id(), cmd.control_status.fuji.command, isprint(cmd.control_status.fuji.command) ? (char)cmd.control_status.fuji.command : '.', current_network_unit);
#endif

    // Debug_printv("cmd (looking for network_unit in byte 6, i.e. hex[5]):\r\n%s\r\n", mstr::toHex(cmd.decoded, 9).c_str());

    if (cmd.control_status.fuji.command != NETCMD_OPEN && current_network_data.json == nullptr) {
        Debug_printv("control should not be called on a non-open channel - FN was probably reset");
    }

    switch (cmd.control_status.fuji.command)
    {
    case NETCMD_SET_CHANNEL:
        current_network_unit = data_buffer[0];
        break;
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
        process_fs(cmd.control_status.fuji.command);
        break;

    case NETCMD_CONTROL:
    case NETCMD_CLOSE_CLIENT:
        process_tcp(cmd.control_status.fuji.command);
        break;

    case NETCMD_SET_CHANNEL_MODE:
        process_http(cmd.control_status.fuji.command);
        break;

    case NETCMD_GET_REMOTE:
    case NETCMD_SET_DESTINATION:
        process_udp(cmd.control_status.fuji.command);
        break;

    default:
        err = SP_ERR::IOERROR;
        break;
    }

    if (err != SP_ERR::NOERROR)
        err_result = SP_ERR::IOERROR;

    send_reply_packet(err_result);
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
    current_network_data.deviceSpec = util_devicespec_fix_for_parsing(d, current_network_data.prefix, data_buffer[0] == 6, false);
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
        err = SP_ERR::BADCTLPARM;
        return;
    }

#ifdef VERBOSE_PROTOCOL
    Debug_printf("::parse_and_instantiate_protocol -> spec: >%s<, url: >%s<\r\n", current_network_data.deviceSpec.c_str(), current_network_data.urlParser->mRawUrl.c_str());
#endif

    // Instantiate protocol object.
    if (!instantiate_protocol())
    {
        Debug_printf("Could not open protocol. spec: >%s<, url: >%s<\n", current_network_data.deviceSpec.c_str(), current_network_data.urlParser->mRawUrl.c_str());
        err = SP_ERR::BADCMD;
        return;
    }
}

void iwmNetwork::iwmnet_set_translation()
{
}

void iwmNetwork::iwmnet_set_timer_rate()
{
}

void iwmNetwork::process_fs(fujiCommandID_t fuji_command)
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
        err = SP_ERR::IOERROR;
        return;
    }

    fujiError_t cmd_err;
    auto url = current_network_data.urlParser.get();
    switch (fuji_command)
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
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        err = SP_ERR::IOERROR;
}

void iwmNetwork::process_tcp(fujiCommandID_t fuji_command)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really a TCP protocol instance
    NetworkProtocolTCP *tcp = dynamic_cast<NetworkProtocolTCP *>(current_network_data.protocol.get());
    if (!tcp)
    {
        err = SP_ERR::IOERROR;
        return;
    }

    fujiError_t cmd_err;
    switch (fuji_command)
    {
    case NETCMD_CONTROL:
        cmd_err = tcp->accept_connection();
        break;
    case NETCMD_CLOSE_CLIENT:
        cmd_err = tcp->close_client_connection();
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        break;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        err = SP_ERR::IOERROR;
}

void iwmNetwork::process_http(fujiCommandID_t fuji_command)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really an HTTP protocol instance
    NetworkProtocolHTTP *http = dynamic_cast<NetworkProtocolHTTP *>(current_network_data.protocol.get());
    if (!http)
    {
        err = SP_ERR::IOERROR;
        return;
    }

    fujiError_t cmd_err;
    switch (fuji_command)
    {
    case NETCMD_SET_CHANNEL_MODE:
        cmd_err = http->set_channel_mode((netProtoHTTPChannelMode_t) data_buffer[1]);
        break;
    default:
        cmd_err = FUJI_ERROR::UNSPECIFIED;
        return;
    }

    if (cmd_err != FUJI_ERROR::NONE)
        err = SP_ERR::IOERROR;
}

void iwmNetwork::process_udp(fujiCommandID_t fuji_command)
{
    auto& current_network_data = network_data_map[current_network_unit];
    // Make sure this is really a UDP protocol instance
    NetworkProtocolUDP *udp = dynamic_cast<NetworkProtocolUDP *>(current_network_data.protocol.get());
    if (!udp)
    {
        err = SP_ERR::IOERROR;
        return;
    }

    fujiError_t cmd_err;
    switch (fuji_command)
    {
#ifndef ESP_PLATFORM
    case NETCMD_GET_REMOTE:
        cmd_err = udp->get_remote(data_buffer, SPECIAL_BUFFER_SIZE);
        SYSTEM_BUS.iwm_send_packet(id(), iwm_packet_type_t::data, SP_ERR::NOERROR,
                                   data_buffer, SPECIAL_BUFFER_SIZE);
        break;
#endif /* ESP_PLATFORM */
    case NETCMD_SET_DESTINATION:
        {
            ByteBuffer buffer(data_len, 0);
            SYSTEM_BUS.transaction_accept(TRANS_STATE::WILL_GET);
            SYSTEM_BUS.transaction_get(buffer.data(), buffer.size());
            cmd_err = udp->set_destination(buffer.data(), buffer.size());
            if (cmd_err != FUJI_ERROR::NONE)
                err = SP_ERR::IOERROR;
            else
                SYSTEM_BUS.transaction_success();
        }
        break;
    default:
        err = SP_ERR::IOERROR;
        break;
    }
}

#endif /* BUILD_APPLE */
