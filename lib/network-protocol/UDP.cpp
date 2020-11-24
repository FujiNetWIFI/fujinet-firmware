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
        Debug_printf("Setting destination hostname to: %s\n",urlParser->hostName.c_str());
        dest = urlParser->hostName;
    }

    // Port must be set, or we bail.
    if (urlParser->port.empty())
    {
        Debug_printf("Port is empty, aborting.\n");
        return true;
    }

    // Attempt to bind port.
    Debug_printf("Binding port %u\n",atoi(urlParser->port.c_str()));
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
