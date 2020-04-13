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
        headerIndex = 0;
        numHeaders = client.headers();
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

    if (headers)
    {
        if (headerIndex < numHeaders)
        {
            strcpy((char *)rx_buf, client.header(headerIndex++).c_str());
            return true;
        }
        else
            return false;
    }
    else
    {
        c = client.getStreamPtr();

        if (c == nullptr)
            return false;

        if (c->readBytes(rx_buf, len) != len)
            return false;

        return true;
    }
}

bool networkProtocolHTTP::write(byte *tx_buf, unsigned short len)
{
    WiFiClient *c;

    if (headers)
    {
        String headerKey;
        String headerValue;
        char tmpKey[256];
        char tmpValue[256];
        char *p = strtok((char *)tx_buf, ":");

        strcpy(tmpKey, p);
        p = strtok(NULL, "");
        strcpy(tmpValue, p);
        headerKey = String(tmpKey);
        headerValue = String(tmpValue);
        client.addHeader(headerKey, headerValue);
    }
    else
    {
        if (!requestStarted)
        {
            if (!startConnection(tx_buf, len))
                return false;
        }
    }

    return true;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    WiFiClient *c;
    int a; // available bytes

    status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0;

    if (!requestStarted)
    {
        if (!startConnection(status_buf, 4))
            return false;
    }

    if (headers)
    {
        if (headerIndex < numHeaders)
        {
            status_buf[0] = client.header(headerIndex).length() & 0xFF;
        }
    }
    else
    {
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
}

bool networkProtocolHTTP::special_supported_00_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 'H': // toggle headers
        return true;
    default:
        return false;
    }

    return false;
}

void networkProtocolHTTP::special_header_toggle(unsigned char aux1)
{
    headers = (aux1 == 1 ? true : false);
}

bool networkProtocolHTTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'H': // toggle headers
        special_header_toggle(cmdFrame->aux1);
        return true;
    default:
        return false;
    }
    return false;
}