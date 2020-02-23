#include "networkProtocolTCP.h"

networkProtocolTCP::networkProtocolTCP()
{
}

networkProtocolTCP::~networkProtocolTCP()
{
    if (server != NULL)
    {
        server->stop();
        delete server;
    }
}

bool networkProtocolTCP::open(networkDeviceSpec *spec)
{
    bool ret;

    if (spec->path[0] == 0x00)
        server = new WiFiServer(spec->port);
    else
        client.connect(spec->path, spec->port);

    if (client.connected() || (server != NULL))
        ret = true;
    else
        ret = false;

    return ret;
}

bool networkProtocolTCP::close()
{
    if (server == NULL)
        client.stop();
    else
        server->stop();

    return true;
}

bool networkProtocolTCP::read(byte *rx_buf, unsigned short len)
{
    return (client.readBytes(rx_buf, len) == len);
}

bool networkProtocolTCP::write(byte *tx_buf, unsigned short len)
{
    return (client.write((char *)&tx_buf), len);
}

bool networkProtocolTCP::status(byte *status_buf)
{
    memset(status_buf, 0x00, 4);
    if (client.connected())
    {
        status_buf[0] = client.available() & 0xFF;
        status_buf[1] = client.available() >> 8;
        status_buf[3] = 1;
    }
    if (server != NULL)
        status_buf[2] = server->available();
}

bool networkProtocolTCP::special(byte* sp_buf, unsigned short len, cmdFrame_t* cmdFrame)
{
    bool ret;

    switch(cmdFrame->comnd)
    {
        case 'A':
            ret=special_accept_connection();
            break;
    }

    return ret;
}

bool networkProtocolTCP::special_accept_connection()
{
    bool ret;

    if (server == NULL)
    {
        return false;
    }
    else
    {
        client=server->accept();
    }
}