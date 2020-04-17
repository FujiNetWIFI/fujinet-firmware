#include "networkProtocolHTTP.h"
#include "debug.h"

networkProtocolHTTP::networkProtocolHTTP()
{
    c = nullptr;
}

networkProtocolHTTP::~networkProtocolHTTP()
{
    for (int i = 0; i < headerCollectionIndex; i++)
    {
        free(headerCollection[i]);
    }

    client.end();
}

bool networkProtocolHTTP::startConnection(byte *buf, unsigned short len)
{
    bool ret = false;

    switch (openMode)
    {
    case GET:
        if (headerCollectionIndex > 0)
            client.collectHeaders((const char **)headerCollection, headerCollectionIndex);

        resultCode = client.GET();

        headerIndex = 0;
        numHeaders = client.headers();
        ret = true;
        break;
    case POST:
        resultCode = client.POST(buf, len);
        ret = true;
        break;
    case PUT:
        resultCode = client.PUT(buf, len);
        ret = true;
        break;
    default:
        ret = false;
        break;
    }

    requestStarted = ret;

    if (requestStarted)
        c = client.getStreamPtr();

    return ret;
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
    return false;
}

bool networkProtocolHTTP::read(byte *rx_buf, unsigned short len)
{
    if (!requestStarted)
    {
        if (!startConnection(rx_buf, len))
            return true;
    }

    if (headers)
    {
        if (headerIndex < numHeaders)
        {
            strncpy((char *)rx_buf, client.header(headerIndex++).c_str(), len);
            return false;
        }
        else
            return true;
    }
    else if (collectHeaders)
    {
        // collect headers is write only. Return error.
        return true;
    }
    else
    {
        if (c == nullptr)
            return true;

        if (c->readBytes(rx_buf, len) != len)
            return true;
    }
    return false;
}

bool networkProtocolHTTP::write(byte *tx_buf, unsigned short len)
{
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
    else if (collectHeaders)
    {
        headerCollection[headerCollectionIndex] = (char *)malloc(len);
        strncpy(headerCollection[headerCollectionIndex++], (char *)tx_buf, len);

        return false;
    }
    else
    {
        if (!requestStarted)
        {
            if (!startConnection(tx_buf, len))
                return true;
        }
    }

    return false;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    int a; // available bytes

    status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0;

    if (!requestStarted)
    {
        if (!startConnection(status_buf, 4))
            return true;
    }

    if (headers)
    {
        if (headerIndex < numHeaders)
        {
            status_buf[0] = client.header(headerIndex).length() & 0xFF;
            status_buf[1] = client.header(headerIndex).length() >> 8;
            status_buf[2] = resultCode & 0xFF;
            status_buf[3] = resultCode >> 8;

            return false; // no error
        }
    }
    else
    {
        if (c == nullptr)
            return true;

        // Limit to reporting max of 65535 bytes available.
        a = (c->available() > 65535 ? 65535 : c->available());

        status_buf[0] = a & 0xFF;
        status_buf[1] = a >> 8;
        status_buf[2] = resultCode & 0xFF;
        status_buf[3] = resultCode >> 8;

        return false; // no error
    }
    return true;
}

bool networkProtocolHTTP::special_supported_00_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 'G': // toggle collect headers
        return true;
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

void networkProtocolHTTP::special_collect_headers_toggle(unsigned char aux1)
{
    collectHeaders = (aux1 == 1 ? true : false);
}

bool networkProtocolHTTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'G': // toggle collect headers
        special_collect_headers_toggle(cmdFrame->aux1);
        return false;
    case 'H': // toggle headers
        special_header_toggle(cmdFrame->aux1);
        return false;
    default:
        return true;
    }
    return true;
}