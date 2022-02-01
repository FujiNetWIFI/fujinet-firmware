/**
 * UDP socket implementation
 */

#include "UDP.h"

#include "../../include/debug.h"

#include "status_error_codes.h"
#include "fnDNS.h"



NetworkProtocolUDP::NetworkProtocolUDP(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolUDP::ctor\n");
}

NetworkProtocolUDP::~NetworkProtocolUDP()
{
    Debug_printf("NetworkProtocolUDP::dtor\n");
}

bool NetworkProtocolUDP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolUDP::open(%s:%s)\n", urlParser->hostName.c_str(), urlParser->port.c_str());

    // Set destination to hostname, if set.
    if (!urlParser->hostName.empty())
    {
        Debug_printf("Setting destination hostname to: %s\n", urlParser->hostName.c_str());
        dest = urlParser->hostName;
    }

    // Port must be set, or we bail.
    if (urlParser->port.empty())
    {
        Debug_printf("Port is empty, aborting.\n");
        return true;
    }
    else
    {
        Debug_printf("Setting destination port to: %s\n", urlParser->port.c_str());
        port = atoi(urlParser->port.c_str());
    }

    // Attempt to bind port.
    Debug_printf("Binding port %u\n", atoi(urlParser->port.c_str()));
    if (udp.begin(atoi(urlParser->port.c_str())) == false)
    {
        errno_to_error();
        return true;
    }
    else
    {
        dest = urlParser->hostName;
        port = atoi(urlParser->port.c_str());
        Debug_printf("After begin: %s:%u\n", dest.c_str(), port);
    }

    // call base class
    NetworkProtocol::open(urlParser, cmdFrame);

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
    uint8_t *newData = (uint8_t *)malloc(len);
    string newString;

    Debug_printf("NetworkProtocolUDP::read(%u)\n", len);

    if (newData == nullptr)
    {
        Debug_printf("Could not allocate %u bytes! Aborting!\n");
        return true; // error.
    }

    if (receiveBuffer->length() == 0)
    {
        if (udp.available() == 0)
        {
            errno_to_error();
            return true;
        }

        // Do the read.
        udp.read(newData, len);

        // Add new data to buffer.
        newString = string((char *)newData, len);
        *receiveBuffer += newString;

        free(newData);
    }

    // Return success
    Debug_printf("errno = %u\n", errno);
    error = 1;
    return NetworkProtocol::read(len);
}

bool NetworkProtocolUDP::write(unsigned short len)
{
    // Call base class to do translation.
    len = translate_transmit_buffer();

    Debug_printf("NetworkProtocolUDP::write(%u,%s,%u)\n", len, dest.c_str(), port);

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

    if (receiveBuffer->length() > 0)
        status->rxBytesWaiting = receiveBuffer->length();
    else
    {
        in_addr_t addr = udp.remoteIP();

        status->rxBytesWaiting = udp.parsePacket();
        
        // Only change dest if we need to.
        if (udp.remoteIP() != IPADDR_NONE)
        {
            dest = string(inet_ntoa(addr));
            port = udp.remotePort();
        }
    }

    status->connected = 1; // Always 'connected'
    status->error = error;

    NetworkProtocol::status(status);

    return false;
}

uint8_t NetworkProtocolUDP::special_inquiry(uint8_t cmd)
{
    Debug_printf("NetworkProtocolUDP::special_inquiry(%02x)\n", cmd);

    switch (cmd)
    {
    case 'D':
        return 0x80;
    }

    return 0xFF;
}

bool NetworkProtocolUDP::special_00(cmdFrame_t *cmdFrame)
{
    return true; // none implemented.
}

bool NetworkProtocolUDP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return true; // none implemented.
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

bool NetworkProtocolUDP::set_destination(uint8_t *sp_buf, unsigned short len)
{
    string path((const char *)sp_buf, len);
    int device_colon = path.find_first_of(":");
    int port_colon = path.find_last_of(":");

    if (device_colon == string::npos)
        return true;

    if (port_colon == device_colon)
        return true;

    string new_dest_str = path.substr(device_colon + 1, port_colon - 2);
    string new_port_str = path.substr(port_colon + 1);

    Debug_printf("New Destination %s port %s\n", new_dest_str.c_str(), new_port_str.c_str());

    port = atoi(new_port_str.c_str());
    dest = new_dest_str;

    return false; // no error.
}

bool NetworkProtocolUDP::is_multicast()
{
    return multicast_write;
}

bool NetworkProtocolUDP::is_multicast(string h)
{
    return is_multicast(get_ip4_addr_by_name(h.c_str()));
}

bool NetworkProtocolUDP::is_multicast(in_addr_t a)
{
    uint32_t address = ntohl(a);
    return (address & 0xF0000000) == 0xE0000000;
}
