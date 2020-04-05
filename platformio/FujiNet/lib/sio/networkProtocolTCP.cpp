#include "networkProtocolTCP.h"

networkProtocolTCP::networkProtocolTCP()
{
#ifdef DEBUG
    Debug_printf("networkProtocolTCP::ctor\n");
#endif
    server = NULL;
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
        server = NULL;
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
        connectionIsServer=true;
    }
    else
    {
#ifdef DEBUG
        Debug_printf("Connecting to host %s port %d\n", spec->path, spec->port);
#endif
        connectionIsServer=false;
        client.connect(spec->path, spec->port);
    }

    if (client.connected() || (server != NULL))
    {
        ret = true;
        client_error_code=0;
    }
    else
    {
        ret = false;
        client_error_code=170;
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
        server->stop();
    }
    return true;
}

bool networkProtocolTCP::read(byte *rx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP read %d bytes\n",len);
#endif
    if (!client.connected())
    {
        client_error_code=128;
        return false;
    }
    
    return (client.readBytes(rx_buf, len) == len);
}

bool networkProtocolTCP::write(byte *tx_buf, unsigned short len)
{
#ifdef DEBUG
    Debug_printf("TCP write %d bytes\n",len);
#endif
    if (!client.connected())
    {
        client_error_code=128;
        return false;
    }

    return (client.write((char *)tx_buf), len);
}

bool networkProtocolTCP::status(byte *status_buf)
{
    memset(status_buf, 0x00, 4);
    if (client.connected())
    {
        status_buf[0] = client.available() & 0xFF;
        status_buf[1] = client.available() >> 8;
        status_buf[2] = client.connected();
        status_buf[3] = client_error_code;
    }
    else if ((server!=NULL) && (server->available()))
    {
        status_buf[2] = server->available();
    }
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
