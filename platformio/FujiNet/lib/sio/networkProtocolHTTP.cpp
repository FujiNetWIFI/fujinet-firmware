#include "networkProtocolHTTP.h"

networkProtocolHTTP::networkProtocolHTTP()
{
}

networkProtocolHTTP::~networkProtocolHTTP()
{
}

bool networkProtocolHTTP::startConnection(byte *buf, unsigned short len)
{
    switch (openMode)
    {
    case GET:
        resultCode = client.GET();
        break;
    case POST:
        resultCode = client.POST(buf, len);
        break;
    case PUT:
        resultCode = client.PUT(buf, len);
        break;
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
        if (!startConnection(rx_buf, len))
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
        if (!startConnection(tx_buf, len))
            return false;
    }

    return true;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    WiFiClient *c;
    int a; // available bytes

    if (!requestStarted)
    {
        if (!startConnection(status_buf,4))
            return false;
    }

    c = client.getStreamPtr();

    if (c == nullptr)
        return false;

    // Limit to reporting max of 65535 bytes available.
    a = (c->available() > 65535 ? 65535 : c->available());

    status_buf[0] = a & 0xFF;
    status_buf[1] = a >> 8;
    status_buf[2] = resultCode & 0xFF;
    status_buf[3] = resultCode >> 8;

    return true;
}

bool networkProtocolHTTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}