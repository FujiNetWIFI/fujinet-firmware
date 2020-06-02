#include "networkProtocolHTTP.h"
#include "debug.h"

networkProtocolHTTP::networkProtocolHTTP()
{
    c = nullptr;
    httpState = DATA;
    requestStarted = false;
}

networkProtocolHTTP::~networkProtocolHTTP()
{
    for (int i = 0; i < headerCollectionIndex; i++)
        free(headerCollection[i]);

    client.end();
}

bool networkProtocolHTTP::startConnection(byte *buf, unsigned short len)
{
    bool ret = false;

#ifdef DEBUG
    Debug_printf("startConnection()\n");
#endif

    switch (openMode)
    {
    case GET:
        client.collectHeaders((const char **)headerCollection, (const size_t)headerCollectionIndex);

        resultCode = client.GET();

        headerIndex = 0;
        numHeaders = client.headers();
        ret = true;
        break;
    case POST:
        resultCode = client.POST(buf, len);
        numHeaders = client.headers();
        headerIndex = 0;
        ret = true;
        break;
    case PUT:
        // Don't start connection here.
        ret = true;
        break;
    default:
        ret = false;
        break;
    }

    requestStarted = ret;

/*
    if (requestStarted)
        c = client.getStreamPtr();
        */

#ifdef DEBUG
    Debug_printf("Result code: %d\n", resultCode);
#endif

    return ret;
}

bool networkProtocolHTTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->aux1)
    {
    case 4:
    case 12:
        openMode = GET;
        break;
    case 13:
        openMode = POST;
        break;
    case 8:
    case 14:
        openMode = PUT;
        break;
    }

    if (urlParser->scheme == "HTTP")
        urlParser->scheme = "http";
    else if (urlParser->scheme == "HTTPS")
        urlParser->scheme = "https";

    if (urlParser->port.empty())
    {
        if (urlParser->scheme == "http")
            urlParser->port = "80";
        else if (urlParser->scheme == "https")
            urlParser->port = "443";
    }

    openedUrlParser = urlParser;
    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();

    if (openMode == PUT)
    {
        fpPUT = fnSystem.make_tempfile(nPUT);
    }

    return client.begin(openedUrl.c_str());
}

bool networkProtocolHTTP::close()
{
    size_t putPos;
    uint8_t* putBuf;

    // Close and Remove temporary PUT file, if needed.
    if (openMode == PUT)
    {
        putPos=ftell(fpPUT);
        putBuf=(uint8_t *)malloc(putPos);
        rewind(fpPUT);
        fread(putBuf,1,putPos,fpPUT);
        client.PUT(putBuf,putPos);
        fclose(fpPUT);
        unlink(nPUT);
        free(putBuf);
    }

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

    switch (httpState)
    {
    case DATA:
        if (c == nullptr)
            return true;

        if (c->read(rx_buf, len) != len)
            return true;
        break;
    case HEADERS:
        if (headerIndex < numHeaders)
        {
            strncpy((char *)rx_buf, client.header(headerIndex++).c_str(), len);
        }
        else
            return true;
        break;
    case COLLECT_HEADERS:
        // collect headers is write only. Return error.
        return true;
    case CA:
        // CA is write only. Return error.
        return true;
    }

    return false;
}

bool networkProtocolHTTP::write(byte *tx_buf, unsigned short len)
{
    int b;
    String headerKey;
    String headerValue;
    char tmpKey[256];
    char tmpValue[256];
    char *p;

    switch (httpState)
    {
    case DATA:
        if (openMode == PUT)
        {
            if (!fpPUT)
                return true;
            
            if (fwrite(tx_buf,1,len,fpPUT) != len)
                return true;
        }
        else
        {
            if (!requestStarted)
            {
                if (!startConnection(tx_buf, len))
                    return true;
            }
        }
        break;
    case HEADERS:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        p = strtok((char *)tx_buf, ":");

        strcpy(tmpKey, p);
        p = strtok(NULL, "");
        strcpy(tmpValue, p);
        headerKey = String(tmpKey);
        headerValue = String(tmpValue);
        client.addHeader(headerKey, headerValue);
#ifdef DEBUG
        Debug_printf("headerKey: %s\n", headerKey.c_str());
        Debug_printf("headerValue: %s\n", headerValue.c_str());
#endif
        break;
    case COLLECT_HEADERS:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        headerCollection[headerCollectionIndex++] = strndup((const char *)tx_buf, len);
        break;
    case CA:
        for (b = 0; b < len; b++)
        {
            if (tx_buf[b] == 0x9B || tx_buf[b] == 0x0A || tx_buf[b] == 0x0D)
                tx_buf[b] = 0x00;
        }

        if (strlen(cert) + strlen((const char *)tx_buf) < sizeof(cert))
        {
            strcat(cert, (const char *)tx_buf);
            strcat(cert, "\n");
#ifdef DEBUG
            Debug_printf("(%d) %s\n", strlen(cert), cert);
#endif
        }
        break;
    }

    return false;
}

bool networkProtocolHTTP::status(byte *status_buf)
{
    int a; // available bytes

    status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0;

    switch (httpState)
    {
    case DATA:
        if (requestStarted == false)
        {
            if (!startConnection(status_buf, 4))
                return true;
        }

        if (c == nullptr)
            return true;

        // Limit to reporting max of 65535 bytes available.
        a = (c->available() > 65535 ? 65535 : c->available());

        status_buf[0] = a & 0xFF;
        status_buf[1] = a >> 8;
        status_buf[2] = resultCode & 0xFF;
        status_buf[3] = resultCode >> 8;
        assertInterrupt = a > 0;
        break;
    case HEADERS:
        if (headerIndex < numHeaders)
        {
            status_buf[0] = client.header(headerIndex).length() & 0xFF;
            status_buf[1] = client.header(headerIndex).length() >> 8;
            status_buf[2] = resultCode & 0xFF;
            status_buf[3] = resultCode >> 8;
        }
        break;
    case COLLECT_HEADERS:
        status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0xFF;
        break;
    case CA:
        status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0xFE;
        break;
    }

    return false;
}

bool networkProtocolHTTP::special_supported_00_command(unsigned char comnd)
{
    switch (comnd)
    {
    case 'G': // toggle collect headers
        return true;
    case 'H': // toggle headers
        return true;
    case 'I': // Get Certificate
        return true;
    default:
        return false;
    }

    return false;
}

void networkProtocolHTTP::special_header_toggle(unsigned char a)
{
    httpState = (a == 1 ? HEADERS : DATA);
}

void networkProtocolHTTP::special_collect_headers_toggle(unsigned char a)
{
    httpState = (a == 1 ? COLLECT_HEADERS : DATA);
}

void networkProtocolHTTP::special_ca_toggle(unsigned char a)
{
    httpState = (a == 1 ? CA : DATA);
    switch (a)
    {
    case 0:
        if (strlen(cert) > 0)
        {
            client.end();
            client.begin(openedUrl.c_str(), cert);
        }
        break;
    case 1:
        memset(cert, 0, sizeof(cert));
        break;
    }
    if (a > 0)
    {
        memset(cert, 0, sizeof(cert));
    }
}

bool networkProtocolHTTP::isConnected()
{
    if (c != nullptr)
        return c->connected();
    else
        return false;
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
    case 'I': // toggle CA
        special_ca_toggle(cmdFrame->aux1);
        return false;
    default:
        return true;
    }
    return true;
}
