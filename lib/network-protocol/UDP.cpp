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

bool NetworkProtocolUDP::open(PeoplesUrlParser *urlParser, FujiTranslationMode mode)
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
        return true;
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
        return true;
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
    NetworkProtocol::open(urlParser, mode);

    return false; // all good.
}

bool NetworkProtocolUDP::close()
{
    // Call base class.
    NetworkProtocol::close();

    // unbind.
    udp.stop();

    return false; // all good.
}

bool NetworkProtocolUDP::read(unsigned short len)
{
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolUDP::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        if (udp.available() == 0)
        {
            errno_to_error();
            return true;
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

bool NetworkProtocolUDP::write(unsigned short len)
{
    // Call base class to do translation.
    len = translate_transmit_buffer();

    Debug_printf("NetworkProtocolUDP::write(%u,%s,%u)\r\n", len, dest.c_str(), port);

    // Check for client connection
    if (dest.empty())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return len; // error
    }

    // Do the write to client socket.
    if (udp.beginPacket(dest.c_str(), port) == false)
    {
        errno_to_error();
        return true;
    }

    udp.write((uint8_t *)transmitBuffer->data(), len);

    if (udp.endPacket() == false)
    {
        errno_to_error();
        return true;
    }

    // Return success
    error = 1;
    transmitBuffer->erase(0, len);

    return false;
}

bool NetworkProtocolUDP::status(NetworkStatus *status)
{

    if (receiveBuffer->length() == 0)
    {
        in_addr_t addr = udp.remoteIP();

        // Only change dest if we need to.
#ifdef ESP_PLATFORM
        if (udp.remoteIP() != IPADDR_NONE)
#else
        if (available() > 0 && addr != IPADDR_NONE)
#endif
        {
            dest = std::string(compat_inet_ntoa(addr));
            port = udp.remotePort();
        }
    }

    status->connected = 1; // Always 'connected'
    status->error = error;

    NetworkProtocol::status(status);

    return false;
}

FujiDirection NetworkProtocolUDP::special_inquiry(uint8_t cmd)
{
    Debug_printf("NetworkProtocolUDP::special_inquiry(%02x)\r\n", cmd);

    switch (cmd)
    {
    case 'D':           // set destination
        return DIRECTION_WRITE;
#ifndef ESP_PLATFORM
    case 'r':           // get remote
        return DIRECTION_READ;
#endif
    }

    return DIRECTION_INVALID;
}

#ifdef OBSOLETE
bool NetworkProtocolUDP::special_00(cmdFrame_t *cmdFrame)
{
    return true; // none implemented.
}

bool NetworkProtocolUDP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
#ifdef ESP_PLATFORM
    return true; // none implemented.
#else
    switch (cmdFrame->comnd)
    {
    case 'r':
        return get_remote(sp_buf, len);
    default:
        return true;
    }
#endif
}

bool NetworkProtocolUDP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'D':
        return set_destination(sp_buf, len);
    default:
        return true;
    }
    return true;
}
#endif /* OBSOLETE */

bool NetworkProtocolUDP::set_destination(uint8_t *sp_buf, unsigned short len)
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
        return true;

    if (port_colon == device_colon)
        return true;

#ifdef ESP_PLATFORM // TODO review & merge
    std::string new_dest_str = path.substr(device_colon + 1, port_colon - 2);
#else
    std::string new_dest_str = path.substr(device_colon + 1, port_colon - device_colon - 1);
#endif
    std::string new_port_str = path.substr(port_colon + 1);

    Debug_printf("New Destination %s port %s\r\n", new_dest_str.c_str(), new_port_str.c_str());

    port = atoi(new_port_str.c_str());
    dest = new_dest_str;

    return false; // no error.
}

#ifndef ESP_PLATFORM
bool NetworkProtocolUDP::get_remote(uint8_t *sp_buf, unsigned short len)
{
    char port_part[8];

    snprintf(port_part, sizeof port_part, ":%d\x9b", udp.remotePort());
    strlcpy((char *)sp_buf, compat_inet_ntoa(udp.remoteIP()), len);
    strlcat((char *)sp_buf, port_part, len);
    Debug_printf("UDP remote is %s\n", sp_buf);

    return false; // no error.
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
