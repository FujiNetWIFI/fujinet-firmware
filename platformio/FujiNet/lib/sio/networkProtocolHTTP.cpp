#include "networkProtocolHTTP.h"

networkProtocolHTTP::networkProtocolHTTP()
{
}

networkProtocolHTTP::~networkProtocolHTTP()
{
}

bool networkProtocolHTTP::startConnection()
{
    switch (openMode)
    {
        case GET:
            
        case POST:
        case PUT:
    }
}

bool networkProtocolHTTP::open(networkDeviceSpec *spec, cmdFrame_t *cmdFrame)
{
    String url = "http://" + String(spec->path);

    switch (cmdFrame->aux1)
    {
    case 12:
        openMode = GET;
        break;
    case 13:
        openMode = POST;
        break;
    case 14:
        openMode = PUT;
        break;
    }

    return client.begin(url);
}

bool networkProtocolHTTP::close()
{
    client.end();
    return true;
}

bool networkProtocolHTTP::read(byte *rx_buf, unsigned short len)
{
    WiFiClient *c;

    if (!requestStarted)
    {
        if (!startConnection())
            return false;
    }

    c = client.getStreamPtr();

    if (c == nullptr)
        return false;

    if (c->readBytes(rx_buf, len) != len)
        return false;

    return true;
}

bool networkProtocolHTTP::write(byte *tx_buf, unsigned short len)
{
    WiFiClient *c;

    if (!requestStarted)
    {
        if (!startConnection())
            return false;
    }

    c = client.getStreamPtr();

    if (c == nullptr)
        return false;

    if (c->write(tx_buf, len) != len)
        return false;

    return true;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    WiFiClient *c;
    int a; // available bytes

    if (!requestStarted)
    {
        if (!startConnection())
            return false;
    }

    c = client.getStreamPtr();

    if (c == nullptr)
        return false;

    // Limit to reporting max of 65535 bytes available.
    a = (c->available() > 65535 ? 65535 : c->available());

    status_buf[0] = a & 0xFF;
    status_buf[1] = a >> 8;
    status_buf[2] = c->connected();
    status_buf[3] = 0;

    return true;
}

bool networkProtocolHTTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}