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

    Debug_printf("NetworkProtocolTCP::read(%u)\n", len);

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

        // Do the socket read.
        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            return true;
        }
        else if (actual_len != len) // Read was short and timed out.
        {
            Debug_printf("Short receive. We got %u bytes, returning %u bytes and ERROR\n", actual_len, len);
            error = NETWORK_ERROR_SOCKET_TIMEOUT;
            return true;
        }

        // Add new data to buffer.
        newString = string((char *)newData, len);
        *receiveBuffer += newString;

        free(newData);
    }
    // Return success
    error = 1;
    return NetworkProtocol::read(len);
}