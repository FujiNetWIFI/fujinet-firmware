/**
 * UDP socket implementation
 */

#include "UDP.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "compat_inet.h"
#include "compat_string.h"

#include "../../include/debug.h"

#include "status_error_codes.h"
#include "fnDNS.h"

#include <vector>



NetworkProtocolUDP::NetworkProtocolUDP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolUDP::ctor\r\n");
}

NetworkProtocolUDP::~NetworkProtocolUDP()
{
    Debug_printf("NetworkProtocolUDP::dtor\r\n");
}

netProtoErr_t NetworkProtocolUDP::open(PeoplesUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolUDP::open(%s:%s)\r\n", urlParser->host.c_str(), urlParser->port.c_str());

#ifdef ESP_PLATFORM
// Set destination to hostname, if set.
    if (!urlParser->host.empty())
#endif
    {
        Debug_printf("Setting destination hostname to: %s\r\n", urlParser->host.c_str());
        dest = urlParser->host;
    }

    // Port must be set, or we bail.
    if (urlParser->port.empty())
    {
        Debug_printf("Port is empty, aborting.\r\n");
        return NETPROTO_ERR_UNSPECIFIED;
    }
    else
    {
        Debug_printf("Setting destination port to: %s\r\n", urlParser->port.c_str());
        port = atoi(urlParser->port.c_str());
    }

    // Attempt to bind port.
#ifdef ESP_PLATFORM
    unsigned short bind_port = port;
#else
    // set listening port (empty target host) or use any src port (target host is not empty)
    unsigned short bind_port = dest.empty() ? port : 0;
#endif
    Debug_printf("Binding port %u\r\n", bind_port);
    if (udp.begin(bind_port) == false)
    {
        errno_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }
    else
    {
#ifdef ESP_PLATFORM // TODO apc: should be set already
        dest = urlParser->host;
        port = atoi(urlParser->port.c_str());
#endif
        Debug_printf("After begin: %s:%u\r\n", dest.c_str(), port);
    }

    // call base class
    NetworkProtocol::open(urlParser, cmdFrame);

    return NETPROTO_ERR_NONE; // all good.
}

netProtoErr_t NetworkProtocolUDP::close()
{
    // Call base class.
    NetworkProtocol::close();

    // unbind.
    udp.stop();

    return NETPROTO_ERR_NONE; // all good.
}

netProtoErr_t NetworkProtocolUDP::read(unsigned short len)
{
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolUDP::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        if (udp.available() == 0)
        {
            errno_to_error();
            return NETPROTO_ERR_UNSPECIFIED;
        }

        // Do the read.
        udp.read(newData.data(), len);

        // Add new data to buffer.
        receiveBuffer->insert(receiveBuffer->end(),newData.begin(),newData.end());
    }

    // Return success
    Debug_printf("errno = %u\r\n", errno);
    error = 1;

    return NetworkProtocol::read(len);
}

netProtoErr_t NetworkProtocolUDP::write(unsigned short len)
{
    // Call base class to do translation.
    len = translate_transmit_buffer();

    Debug_printf("NetworkProtocolUDP::write(%u,%s,%u)\r\n", len, dest.c_str(), port);

    // Check for client connection
    if (dest.empty())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return NETPROTO_ERR_UNSPECIFIED; // error
    }

    // Do the write to client socket.
    if (udp.beginPacket(dest.c_str(), port) == false)
    {
        errno_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    udp.write((uint8_t *)transmitBuffer->data(), len);

    if (udp.endPacket() == false)
    {
        errno_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // Return success
    error = 1;
    transmitBuffer->erase(0, len);

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolUDP::status(NetworkStatus *status)
{

    if (receiveBuffer->length() > 0)
        status->rxBytesWaiting = receiveBuffer->length();
    else
    {
        in_addr_t addr = udp.remoteIP();

        status->rxBytesWaiting = udp.parsePacket();

        // Only change dest if we need to.
#ifdef ESP_PLATFORM
        if (udp.remoteIP() != IPADDR_NONE)
#else
        if (status->rxBytesWaiting > 0 && addr != IPADDR_NONE)
#endif
        {
            dest = std::string(compat_inet_ntoa(addr));
            port = udp.remotePort();
        }
    }

    status->connected = 1; // Always 'connected'
    status->error = error;

    NetworkProtocol::status(status);

    return NETPROTO_ERR_NONE;
}

AtariSIODirection NetworkProtocolUDP::special_inquiry(fujiCommandID_t cmd)
{
    Debug_printf("NetworkProtocolUDP::special_inquiry(%02x)\r\n", cmd);

    switch (cmd)
    {
    case FUJICMD_SET_DESTINATION:
        return SIO_DIRECTION_WRITE;
#ifndef ESP_PLATFORM
    case FUJICMD_GET_REMOTE:
        return SIO_DIRECTION_READ;
#endif
    default:
        break;
    }

    return SIO_DIRECTION_INVALID;
}

netProtoErr_t NetworkProtocolUDP::special_00(cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_UNSPECIFIED; // none implemented.
}

netProtoErr_t NetworkProtocolUDP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
#ifdef ESP_PLATFORM
    return NETPROTO_ERR_UNSPECIFIED; // none implemented.
#else
    switch (cmdFrame->comnd)
    {
    case FUJICMD_GET_REMOTE:
        return get_remote(sp_buf, len);
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
#endif
}

netProtoErr_t NetworkProtocolUDP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case FUJICMD_SET_DESTINATION:
        return set_destination(sp_buf, len);
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
    return NETPROTO_ERR_UNSPECIFIED;
}

netProtoErr_t NetworkProtocolUDP::set_destination(uint8_t *sp_buf, unsigned short len)
{
#ifdef ESP_PLATFORM // TODO review & merge
    std::string path((const char *)sp_buf, len);
#else
    util_devicespec_fix_9b(sp_buf, len); // TODO check sp_buf, first byte seems corrupted
    Debug_printf("set_destination %s\n", sp_buf);
    std::string path((const char *)sp_buf);
#endif
    int device_colon = path.find_first_of(":");
    int port_colon = path.find_last_of(":");

    if (device_colon == std::string::npos)
        return NETPROTO_ERR_UNSPECIFIED;

    if (port_colon == device_colon)
        return NETPROTO_ERR_UNSPECIFIED;

#ifdef ESP_PLATFORM // TODO review & merge
    std::string new_dest_str = path.substr(device_colon + 1, port_colon - 2);
#else
    std::string new_dest_str = path.substr(device_colon + 1, port_colon - device_colon - 1);
#endif
    std::string new_port_str = path.substr(port_colon + 1);

    Debug_printf("New Destination %s port %s\r\n", new_dest_str.c_str(), new_port_str.c_str());

    port = atoi(new_port_str.c_str());
    dest = new_dest_str;

    return NETPROTO_ERR_NONE; // no error.
}

#ifndef ESP_PLATFORM
netProtoErr_t NetworkProtocolUDP::get_remote(uint8_t *sp_buf, unsigned short len)
{
    char port_part[8];

    snprintf(port_part, sizeof port_part, ":%d\x9b", udp.remotePort());
    strlcpy((char *)sp_buf, compat_inet_ntoa(udp.remoteIP()), len);
    strlcat((char *)sp_buf, port_part, len);
    Debug_printf("UDP remote is %s\n", sp_buf);

    return NETPROTO_ERR_NONE; // no error.
}
#endif

bool NetworkProtocolUDP::is_multicast()
{
    return multicast_write;
}

bool NetworkProtocolUDP::is_multicast(std::string h)
{
    return is_multicast(get_ip4_addr_by_name(h.c_str()));
}

bool NetworkProtocolUDP::is_multicast(in_addr_t a)
{
    uint32_t address = ntohl(a);
    return (address & 0xF0000000) == 0xE0000000;
}
