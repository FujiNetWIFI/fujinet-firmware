#include <string.h>
#include "networkProtocolTCP.h"

networkProtocolTCP::networkProtocolTCP()
{
    Debug_printf("networkProtocolTCP::ctor\n");
    server = nullptr;
    _isConnected=false;
}

networkProtocolTCP::~networkProtocolTCP()
{
    Debug_printf("networkProtocolTCP::dtor\n");
    if (server != nullptr)
    {
        if (client.connected())
            client.stop();

        server->stop();
        delete server;
        server = nullptr;
    }
}

bool networkProtocolTCP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame, enable_interrupt_t enable_interrupt)
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
        server = new fnTcpServer(atoi(urlParser->port.c_str()));
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

bool networkProtocolTCP::close(enable_interrupt_t enable_interrupt)
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

bool networkProtocolTCP::read(uint8_t *rx_buf, unsigned short len)
{
    Debug_printf("TCP read %d bytes\n", len);

    if (!client.connected())
    {
        client_error_code = 128;
        return false;
    }
    return (client.read(rx_buf, len) != len);
}

bool networkProtocolTCP::write(uint8_t *tx_buf, unsigned short len)
{
    Debug_printf("TCP write %d bytes\n", len);

    if (!client.connected())
    {
        client_error_code = 128;
        return false;
    }
    return client.write(tx_buf, len) != len;
}

bool networkProtocolTCP::status(uint8_t *status_buf)
{
    unsigned short available_bytes;

    memset(status_buf, 0x00, 4);
    if (client.connected())
    {
        available_bytes = client.available();
        status_buf[0] = available_bytes & 0xFF;
        status_buf[1] = available_bytes >> 8;
        status_buf[2] = (client.connected() == false ? 0 : 1);
        status_buf[3] = (client.connected() == false ? 136 : 1);
    }
    else if (server != NULL)
    {
        if (!client.connected())
        {
            status_buf[2] = server->hasClient();
            status_buf[3] = (_isConnected==true ? 136 : 1);
        }    
        else
        {
            status_buf[2] = client.connected();
        }
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

bool networkProtocolTCP::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
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
        Debug_printf("accept connection attempted on non-server scoket.");
        return true; // error
    }
    else
    {
        Debug_printf("accepting connection.");
        if (server->hasClient())
        {
            client = server->available();
            _isConnected=true;
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

int networkProtocolTCP::available()
{
    return client.available();
}