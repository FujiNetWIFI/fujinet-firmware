#include <expat.h>
#include <sstream>
#include "networkProtocolHTTP.h"
#include "utils.h"
#include "../../include/debug.h"

class DAVHandler
{
public:
    vector<DAVEntry> entries;
    DAVEntry currentEntry;
    bool insideResponse;
    bool insideDisplayName;
    bool insideGetContentLength;

    void Start(const XML_Char *el, const XML_Char **attr)
    {
        if (strcmp(el, "D:response") == 0)
            insideResponse = true;
        else if (strcmp(el, "D:displayname") == 0)
            insideDisplayName = true;
        else if (strcmp(el, "D:getcontentlength") == 0)
            insideGetContentLength = true;
    }
    void End(const XML_Char *el)
    {
        if (strcmp(el, "D:response") == 0)
        {
            insideResponse = false;
            Debug_printf("Adding Entry: %s %lu\n", currentEntry.filename.c_str(), currentEntry.filesize);
            entries.push_back(currentEntry);
        }
        else if (strcmp(el, "D:displayname") == 0)
            insideDisplayName = false;
        else if (strcmp(el, "D:getcontentlength"))
            insideGetContentLength = false;
    }

    void Char(const XML_Char *s, int len)
    {
        if (insideResponse == true)
        {
            if (insideDisplayName == true)
            {
                currentEntry.filename = string(s, len);
            }
            else if (insideGetContentLength == true)
            {
                stringstream ss(string(s, len));
                ss >> currentEntry.filesize;
            }
        }
    }

    string ToString()
    {
        string output;
        for (vector<DAVEntry>::iterator it = entries.begin(); it != entries.end(); ++it)
        {
            output += util_entry(util_crunch(it->filename), it->filesize) + "\x9b";
        }
        output += "999+FREE SECTORS\x9b";
        return output;
    }

    string ToLongString()
    {
        string output;
        for (vector<DAVEntry>::iterator it = entries.begin(); it != entries.end(); ++it)
        {
            output += util_long_entry(it->filename, it->filesize);
        }
        output += "999+FREE SECTORS\x9b";
        return output;
    }

    vector<DAVEntry> ToEntries()
    {
        return entries;
    }
};

template <class T>
void Start(void *data, const XML_Char *El, const XML_Char **attr)
{
    T *handler = static_cast<T *>(data);
    handler->Start(El, attr);
}

template <class T>
void End(void *data, const XML_Char *El)
{
    T *handler = static_cast<T *>(data);
    handler->End(El);
}

template <class T>
void Char(void *data, const XML_Char *s, int len)
{
    T *handler = static_cast<T *>(data);
    handler->Char(s, len);
}

networkProtocolHTTP::networkProtocolHTTP()
{
    httpState = DATA;
    requestStarted = false;
}

networkProtocolHTTP::~networkProtocolHTTP()
{
    for (int i = 0; i < headerCollectionIndex; i++)
        free(headerCollection[i]);

    // client.end();
    client.close();
}

void networkProtocolHTTP::parseDir()
{
    DAVHandler handler;
    XML_Parser parser = XML_ParserCreate(NULL);
    uint8_t *buf;

#ifdef BOARD_HAS_PSRAM
    buf = (uint8_t *)heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    buf = calloc(1, 16384);
#endif

    XML_SetUserData(parser, &handler);
    XML_SetElementHandler(parser, Start<DAVHandler>, End<DAVHandler>);
    XML_SetCharacterDataHandler(parser, Char<DAVHandler>);

    while (int len = client.read(buf, client.available()))
    {
        int done = len < 16384;
        XML_Status status = XML_Parse(parser, (const char *)buf, len, done);
        if (status == XML_STATUS_ERROR)
        {
            Debug_printf("DAV response XML Parse Error! msg: %s line: %lu\n", XML_ErrorString(XML_GetErrorCode(parser)), XML_GetCurrentLineNumber(parser));
            XML_ParserFree(parser);
            free(buf);
            return;
        }
    }
    Debug_printf("DAV Response Parsed.\n");

    dirEntries = handler.ToEntries();

    if (aux2 == 128)
        dirString = handler.ToLongString();
    else
        dirString = handler.ToString();
    Debug_printf("DAV Response serialized.\n");
    XML_ParserFree(parser);
    free(buf);
}

bool networkProtocolHTTP::startConnection(uint8_t *buf, unsigned short len)
{
    bool ret = false;

    fnSystem.delay(1);

#ifdef DEBUG
    Debug_printf("startConnection()\n");
#endif

    switch (openMode)
    {
    case DIR:
        resultCode = client.PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /></D:prop>\r\n</D:propfind>\r\n");
        if (resultCode == 207)
            parseDir();
        ret = true;
        break;
    case GET:
        client.collect_headers((const char **)headerCollection, headerCollectionIndex);

        resultCode = client.GET();
        if (resultCode == 404)
        {
            string baseurl = openedUrl.substr(0, openedUrl.find_last_of("/"));
            string filename = openedUrl.substr(openedUrl.find_last_of("/") + 1);

            if (client.begin(baseurl + "/") == false)
                return false; // error

            resultCode = client.PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /></D:prop>\r\n</D:propfind>\r\n");
            if (resultCode == 207)
            {
                parseDir();
                for (vector<DAVEntry>::iterator it = dirEntries.begin(); it != dirEntries.end(); ++it)
                {
                    if (util_crunch(it->filename) == util_crunch(filename))
                    {
                        if (client.begin(baseurl + "/" + it->filename) == false)
                            return false; // Error

                        resultCode = client.GET();
                    }
                }
            }
        }

        headerIndex = 0;
        numHeaders = client.get_header_count();
        ret = true;
        break;
    case POST:
        resultCode = client.POST((const char *)buf, len);
        numHeaders = client.get_header_count();
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

#ifdef DEBUG
    Debug_printf("Result code: %d\n", resultCode);
#endif

    return ret;
}

bool networkProtocolHTTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame, enable_interrupt_t enable_interrupt)
{
    aux1 = cmdFrame->aux1;
    aux2 = cmdFrame->aux2;

    switch (cmdFrame->aux1)
    {
    case 6:
        urlParser->path = urlParser->path.substr(0, urlParser->path.find_first_of("*"));
        openMode = DIR;
        break;
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

bool networkProtocolHTTP::close(enable_interrupt_t enable_interrupt)
{
    size_t putPos;
    uint8_t *putBuf;

    // Close and Remove temporary PUT file, if needed.
    if (openMode == PUT)
    {
        putPos = ftell(fpPUT);
        Debug_printf("putPos is %d", putPos);
        putBuf = (uint8_t *)malloc(putPos);
        fseek(fpPUT, 0, SEEK_SET);
        fread(putBuf, 1, putPos, fpPUT);
        Debug_printf("\n");

        resultCode = client.PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /></D:prop>\r\n</D:propfind>\r\n");
        if (resultCode == 404) // not found, try to crunch resolve
        {
            string baseurl = openedUrl.substr(0, openedUrl.find_last_of("/"));
            string filename = openedUrl.substr(openedUrl.find_last_of("/") + 1);

            client.close();

            if (client.begin(baseurl) == false)
                return false; // error

            resultCode = client.PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /></D:prop>\r\n</D:propfind>\r\n");
            if (resultCode == 207)
            {
                bool resolved = false;
                
                parseDir();
                
                for (vector<DAVEntry>::iterator it = dirEntries.begin(); it != dirEntries.end(); ++it)
                {
                    if (util_crunch(it->filename) == util_crunch(filename))
                    {
                        client.close();

                        if (client.begin(baseurl + it->filename) == false)
                            return false; // Error
                        resolved = true;
                    }
                }

                if (resolved==false)
                {
                    client.close();
                    client.begin(openedUrl); // nothing resolved, open with original URL.
                }
            }
            else
            {
                // WEBDAV not supported, re-open with original URL.
                client.close();
                resultCode = client.begin(openedUrl);
            }
        }

        client.PUT((const char *)putBuf, putPos);
        fclose(fpPUT);
        fnSystem.delete_tempfile(nPUT);
        free(putBuf);
    }

    //client.end();
    client.close();
    return true;
}

bool networkProtocolHTTP::read(uint8_t *rx_buf, unsigned short len)
{
    if (!requestStarted)
    {
        if (!startConnection(rx_buf, len))
            return true;
    }

    switch (httpState)
    {
    case CMD:
        // Do nothing.
        break;
    case DATA:
        if (openMode == DIR)
        {
            string fragment = dirString.substr(0, len);
            memcpy(rx_buf, fragment.data(), fragment.size());
            dirString.erase(0, len);
            return false;
        }
        else
        {
            if (client.read(rx_buf, len) != len)
                return true;
            break;
        case HEADERS:
            if (headerIndex < numHeaders)
            {
                //strlcpy((char *)rx_buf, client.header(headerIndex++).c_str(), len);
                client.get_header(headerIndex++, (char *)rx_buf, len);
            }
            else
                return true;
        }
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

bool networkProtocolHTTP::write(uint8_t *tx_buf, unsigned short len)
{
    int b;
    string headerKey;
    string headerValue;
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

            fwrite(tx_buf, 1, len, fpPUT);
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
    case CMD:
        // Do nothing
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

        client.set_header(tmpKey, tmpValue);

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

bool networkProtocolHTTP::status(uint8_t *status_buf)
{
    int a; // available bytes

    status_buf[0] = status_buf[1] = status_buf[2] = status_buf[3] = 0;

    switch (httpState)
    {
    case CMD:
        status_buf[0] = 0;
        status_buf[1] = 0;
        status_buf[2] = 1;
        status_buf[3] = 1;
        break;
    case DATA:
        if (openMode == PUT)
        {
            status_buf[0] = 0;
            status_buf[1] = 0;
            status_buf[2] = 1;
            status_buf[3] = 1;
        }
        else if (openMode == DIR)
        {
            if (requestStarted == false)
                if (!startConnection(status_buf, 4))
                    return true;

            status_buf[0] = dirString.size() & 0xFF;
            status_buf[1] = dirString.size() >> 8;
            status_buf[2] = (dirString.size() > 0 ? 1 : 0);
            status_buf[3] = (dirString.size() > 0 ? 1 : 136);
        }
        else
        {
            if (requestStarted == false)
            {
                if (!startConnection(status_buf, 4))
                    return true;
            }

            a = client.available();
            a = a > 0xFFFF ? 0xFFFF : a;

            status_buf[0] = a & 0xFF;
            status_buf[1] = a >> 8;
            status_buf[3] = (a == 0 ? 136 : 1);
        }
        break;
    case HEADERS:
        if (headerIndex < numHeaders)
        {
            uint16_t ha = client.get_header(headerIndex).length();
            status_buf[0] = ha & 0xFF;
            status_buf[1] = ha >> 8;
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
            client.close();
            client.begin(openedUrl);
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
    return client.available() > 0;
}

bool networkProtocolHTTP::del(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
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

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    //return client.sendRequest("DELETE");
    return client.DELETE();
}

bool networkProtocolHTTP::mkdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
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

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    return client.MKCOL();
}

bool networkProtocolHTTP::rmdir(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
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

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    return client.DELETE();
}

bool networkProtocolHTTP::rename(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    httpState = CMD;
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

    // Remove leading slash!
    urlParser->path = urlParser->path.substr(1);

    // parse away the src, dest file.
    comma_pos = urlParser->path.find(",");

    if (comma_pos == string::npos)
        return false;

    rnFrom = urlParser->path.substr(0, comma_pos);
    rnTo = urlParser->path.substr(comma_pos + 1);
    rnTo = "/" + rnTo;
    urlParser->path = urlParser->path.substr(0, comma_pos);

    openedUrl = urlParser->scheme + "://" + urlParser->hostName + ":" + urlParser->port + "/" + urlParser->path + (urlParser->query.empty() ? "" : ("?") + urlParser->query).c_str();
    client.begin(openedUrl.c_str());

    return client.MOVE(rnTo.c_str(), false);
}

bool networkProtocolHTTP::special(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
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
