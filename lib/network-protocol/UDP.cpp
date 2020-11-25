/**
 * UDP socket implementation
 */

#include "UDP.h"
#include "status_error_codes.h"

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

    // Attempt to bind port.
    Debug_printf("Binding port %u\n", atoi(urlParser->port.c_str()));
    if (udp.begin(atoi(urlParser->port.c_str())) == false)
    {
        errno_to_error();
        return true;
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
    unsigned short actual_len = 0;
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
        if (udp.available()==0)
        {
            errno_to_error();
            return true;
        }

        // Do the read.
        udp.read(newData,len);

        // Add new data to buffer.
        newString = string((char *)newData, len);
        *receiveBuffer += newString;

        free(newData);
    }

    // Return success
    Debug_printf("errno = %u\n",errno);
    error = 1;
    return NetworkProtocol::read(len);
}

bool NetworkProtocolUDP::write(unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolUDP::write(%u)\n", len);

    // Check for client connection
    if (dest.empty())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return len; // error
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    // Do the write to client socket.
    actual_len = udp.write((uint8_t *)transmitBuffer->data(), len);

    if (actual_len != len) // write was short.
    {
        Debug_printf("Short send. We sent %u bytes, but asked to send %u bytes.\n", actual_len, len);
        error = NETWORK_ERROR_SOCKET_TIMEOUT;
        return true;
    }

    // Return success
    error = 1;
    transmitBuffer->erase(0, len);

    return false;
}

bool NetworkProtocolUDP::status(NetworkStatus *status)
{

    status->rxBytesWaiting = udp.available();
    status->reserved = 1; // Always 'connected'
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
    dest = string((char *)sp_buf, len);
    Debug_printf("NetworkProtocolUDP::set_destination(%s)\n",dest.c_str());
    return false; // No error.
}