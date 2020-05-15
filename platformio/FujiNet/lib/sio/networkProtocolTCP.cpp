#include "networkProtocolTCP.h"

networkProtocolTCP::networkProtocolTCP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolTCP::ctor\n");
#endif
    server = nullptr;
}

networkProtocolTCP::~networkProtocolTCP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolTCP::dtor\n");
#endif
    if (server != nullptr)
    {
        if (client.connected())
            client.stop();

        server->stop();
        delete server;
        server = nullptr;
    }
}

bool networkProtocolTCP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    bool ret;

    if (urlParser->port.empty())
        urlParser->port = "23";

#ifdef DEBUG
    Debug_printf("networkProtocolTCP::open %s:%s\n", urlParser->hostName.c_str(), urlParser->port.c_str());
#endif

    if (urlParser->hostName == "")
    {
#ifdef DEBUG
        Debug_printf("Creating server object on port %s\n", urlParser->port.c_str());
#endif
        server = new WiFiServer(atoi(urlParser->port.c_str()));
        server->begin(atoi(urlParser->port.c_str()));
        connectionIsServer = true;
    }
    else
    {
#ifdef DEBUG
        Debug_printf("Connecting to host %s port %s\n", urlParser->path.c_str(), urlParser->port.c_str());
#endif
        connectionIsServer = false;
        client.connect(urlParser->hostName.c_str(), atoi(urlParser->port.c_str()));
    }

    if (client.connected() || (server != NULL))
    {
        ret = true;
        client_error_code = 0;
    }
    else
    {
        ret = false;
        client_error_code = 170;
    }

#ifdef DEBUG
    Debug_printf("Connected? %d\n", ret);
#endif

    return ret;
}

bool networkProtocolTCP::close()
{
    if (server == NULL)
    {
#ifdef DEBUG
        Debug_printf("closing TCP client\n");
#endif
        client.stop();
    }
    else
    {
#ifdef DEBUG
        Debug_printf("closing TCP server\n");
#endif
        if (client.connected())
            client.stop();

        server->stop();
    }
    return true;
}

bool networkProtocolTCP::read(byte *rx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP read %d bytes\n", len);
#endif
    if (!client.connected())
    {
        client_error_code = 128;
        return false;
    }
    assertProceed = true;
    return (client.readBytes(rx_buf, len) != len);
}

bool networkProtocolTCP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP write %d bytes\n", len);
#endif
    if (!client.connected())
    {
        client_error_code = 128;
        return false;
    }
    assertProceed = true;
    return (client.write((char *)tx_buf), len != len);
}

bool networkProtocolTCP::status(byte *status_buf)
{
    unsigned short available_bytes;

    memset(status_buf, 0x00, 4);
    if (client.connected())
    {
        available_bytes = client.available();
        status_buf[0] = available_bytes & 0xFF;
        status_buf[1] = available_bytes >> 8;
        status_buf[2] = client.connected();
        status_buf[3] = client_error_code;
        assertProceed = (available_bytes > 0);
    }
    else if (server != NULL)
    {
        if (!client.connected())
            status_buf[2] = server->hasClient();
        else
            status_buf[2] = client.connected();
    }
    return false;
}

bool networkProtocolTCP::special_supported_00_command(unsigned char comnd)
{
    if (comnd == 'A')
        return true;
    else
        return false;
}

bool networkProtocolTCP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    bool ret = false;

    switch (cmdFrame->comnd)
    {
    case 'A':
        ret = special_accept_connection();
        break;
    }

    return ret;
}

bool networkProtocolTCP::special_accept_connection()
{
    if (server == NULL)
    {
#ifdef DEBUG
        Debug_printf("accept connection attempted on non-server scoket.");
#endif
        return true; // error
    }
    else
    {
#ifdef DEBUG
        Debug_printf("accepting connection.");
#endif
        if (server->hasClient())
        {
            client = server->available();
        }
        else
        {
            return true;    // error.
        }
    }
    return false; // no error.
}

bool networkProtocolTCP::isConnected()
{
    return client.connected();
}