#include "networkProtocolTCP.h"

networkProtocolTCP::networkProtocolTCP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolTCP::ctor\n");
#endif
}

networkProtocolTCP::~networkProtocolTCP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolTCP::dtor\n");
#endif
    if (server != NULL)
    {
        server->stop();
        delete server;
    }
}

bool networkProtocolTCP::open(networkDeviceSpec *spec)
{
    bool ret;

#ifdef DEBUG
    Debug_printf("networkProtocolTCP::open %s\n", spec->toChar());
#endif

    if (spec->path[0] == 0x00)
    {
#ifdef DEBUG
        Debug_printf("Creating server object on port %d\n", spec->port);
#endif
        server = new WiFiServer(spec->port);
    }
    else
    {
#ifdef DEBUG
        Debug_printf("Connecting to host %s port %d\n", spec->path, spec->port);
#endif
        client.connect(spec->path, spec->port);
    }

    if (client.connected() || (server != NULL))
        ret = true;
    else
        ret = false;

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
        server->stop();
    }
    return true;
}

bool networkProtocolTCP::read(byte *rx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP read %d bytes\n",len);
#endif
    return (client.readBytes(rx_buf, len) == len);
}

bool networkProtocolTCP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP write %d bytes\n",len);
#endif 
    return (client.write((char *)&tx_buf), len);
}

bool networkProtocolTCP::status(byte *status_buf)
{
    memset(status_buf, 0x00, 4);
    if (client.connected())
    {
#ifdef DEBUG
        Debug_printf("Available bytes: %d",client.available());
#endif 
        status_buf[0] = client.available() & 0xFF;
        status_buf[1] = client.available() >> 8;
        status_buf[3] = 1;
    }
    if (server != NULL)
    {
#ifdef DEBUG
        Debug_printf("Server connected? %d",server->available());
#endif
        status_buf[2] = server->available();
    }
    return true;
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
        return false;
    }
    else
    {
#ifdef DEBUG
        Debug_printf("accepting connection.");
#endif
        client = server->accept();
    }
    return true;
}