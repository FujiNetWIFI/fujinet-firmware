/**
 * HTTP implementation
 */

#include "HTTP.h"
#include "status_error_codes.h"
#include "../webdav/WebDAV.h"
#include "../utils/utils.h"

/**
 Modes and the N: HTTP Adapter:

Aux1 values
===========

4 = GET, no headers, just grab data.
6 = PROPFIND, WebDAV directory
8 = PUT, write data to server, XIO used to toggle headers to get versus data write
12 = GET, write sets headers to fetch, read grabs data
13 = POST, write sends post data to server, read grabs response, XIO used to change write behavior, toggle headers to get or headers to set.

DELETE, MKCOL, RMCOL, COPY, MOVE, are all handled via idempotent XIO commands.
*/

NetworkProtocolHTTP::NetworkProtocolHTTP(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    fileSize = 0;
    resultCode = 0;
    collect_headers_count = 0;
    returned_header_cursor = 0;
    httpChannelMode = DATA;
}

NetworkProtocolHTTP::~NetworkProtocolHTTP()
{
    for (int i = 0; i < collect_headers_count; i++)
        if (collect_headers[i] != nullptr)
            free(collect_headers[i]);
}

uint8_t NetworkProtocolHTTP::special_inquiry(uint8_t cmd)
{

    switch (cmd)
    {
    case 'M':
        return (aux1_open > 8 ? 0x00 : 0xFF);
    default:
        return 0xFF;
    }
}

bool NetworkProtocolHTTP::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'M':
        return special_set_channel_mode(cmdFrame);
    default:
        return true;
    }
}

bool NetworkProtocolHTTP::special_set_channel_mode(cmdFrame_t *cmdFrame)
{
    bool err = false;

    Debug_printf("NetworkProtocolHTTP::special_set_channel_mode(%u)\n", httpChannelMode);

    switch (cmdFrame->aux2)
    {
    case 0:
        httpChannelMode = DATA;
        fileSize = bodySize;
        break;
    case 1:
        httpChannelMode = COLLECT_HEADERS;
        break;
    case 2:
        httpChannelMode = GET_HEADERS;
        break;
    case 3:
        httpChannelMode = SET_HEADERS;
        break;
    case 4:
        httpChannelMode = SEND_POST_DATA;
        break;
    default:
        error = NETWORK_ERROR_INVALID_COMMAND;
        err = true;
    }

    return err;
}

bool NetworkProtocolHTTP::open_file_handle()
{
    Debug_printf("NetworkProtocolHTTP::open_file_handle()\n");

    error = NETWORK_ERROR_SUCCESS;

    switch (aux1_open)
    {
    case 4:  // GET with no headers, filename resolve
    case 12: // GET with ability to set headers, no filename resolve.
        httpOpenMode = GET;
        break;
    case 8: // WRITE, filename resolve, ignored if not found.
        httpOpenMode = PUT;
        break;
    case 13: // POST can set headers, also no filename resolve
        httpOpenMode = POST;
        break;
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }

    // This is set IF we came back through here via resolve().
    if (resultCode > 399)
    {
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolHTTP::open_dir_handle()
{
    char *buf;
    unsigned short len, actual_len;

    Debug_printf("NetworkProtocolHTTP::open_dir_handle()\n");

    if (client != nullptr)
    {
        delete client;
        client = new fnHttpClient();
        client->begin(opened_url->toString());
    }

    // client->begin already called in mount()
    resultCode = client->PROPFIND(fnHttpClient::webdav_depth::DEPTH_1, "<?xml version=\"1.0\"?>\r\n<D:propfind xmlns:D=\"DAV:\">\r\n<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /></D:prop>\r\n</D:propfind>\r\n");

    if (resultCode > 399)
    {
        Debug_printf("Could not do PROPFIND. Result code %u\n", resultCode);
        fserror_to_error();
        return true;
    }

    len = client->available();
    buf = (char *)malloc(len);

    if (buf == nullptr)
    {
        Debug_printf("Could not allocate %u bytes for PROPFIND data. Aborting\n", len);
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    // Grab the buffer.
    actual_len = client->read((uint8_t *)buf, len);

    if (actual_len != len)
    {
        Debug_printf("Expected %u bytes, actually got %u bytes.\n", len, actual_len);
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    // Parse the buffer
    if (parseDir(buf, len))
    {
        Debug_printf("Could not parse buffer, returning 144\n");
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    // Scoot to beginning of entries.
    dirEntryCursor = webDAV.entries.begin();

    if (client != nullptr)
    {
        delete client;
        client = new fnHttpClient();
        client->begin(opened_url->toString());
    }

    // Directory parsed, ready to be returned by read_dir_entry()
    return false;
}

bool NetworkProtocolHTTP::mount(EdUrlParser *url)
{
    Debug_printf("NetworkProtocolHTTP::mount(%s)\n", url->toString().c_str());

    // fix scheme because esp-idf hates uppercase for some #()$@ reason.
    if (url->scheme == "HTTP")
        url->scheme = "http";
    else if (url->scheme == "HTTPS")
        url->scheme = "https";

    client = new fnHttpClient();

    // fileSize = 65535;

    if (aux1_open == 6)
        util_replaceAll(url->path,"*.*","");

    return !client->begin(url->toString());
}

bool NetworkProtocolHTTP::umount()
{
    Debug_printf("NetworkProtocolHTTP::umount()\n");

    if (client == nullptr)
        return false;

    delete client;

    return false;
}

void NetworkProtocolHTTP::fserror_to_error()
{
    switch (resultCode)
    {
    case 200:
    case 201:
    case 202:
    case 203:
    case 204:
    case 205:
    case 206:
    case 207:
    case 208:
    case 226:
        error = NETWORK_ERROR_SUCCESS;
        break;
    case 401: // Unauthorized
    case 402:
    case 403: // Forbidden
    case 407:
        error = NETWORK_ERROR_INVALID_USERNAME_OR_PASSWORD;
        break;
    case 404:
    case 410:
        error = NETWORK_ERROR_FILE_NOT_FOUND;
        break;
    case 405:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        break;
    case 408:
        error = NETWORK_ERROR_GENERAL_TIMEOUT;
        break;
    case 423:
    case 451:
        error = NETWORK_ERROR_ACCESS_DENIED;
        break;
    case 400: // Bad request
    case 406: // not acceptible
    case 409:
    case 411:
    case 412:
    case 413:
    case 414:
    case 415:
    case 416:
    case 417:
    case 418:
    case 421:
    case 422:
    case 424:
    case 425:
    case 426:
    case 428:
    case 429:
    case 431:
    case 500:
    case 501:
    case 502:
    case 503:
    case 504:
    case 505:
    case 506:
    case 507:
    case 508:
    case 510:
    case 511:
    default:
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

bool NetworkProtocolHTTP::status_file(NetworkStatus *status)
{
    switch (httpChannelMode)
    {
    case DATA:
        if (aux1_open == 4 && resultCode == 0)
            http_transaction();
        status->rxBytesWaiting = client->available() > 65535 ? 65535 : client->available();
        status->connected = client->available() > 0;
        status->error = client->available() > 0 ? error : NETWORK_ERROR_END_OF_FILE;
        return false;
    case SET_HEADERS:
    case COLLECT_HEADERS:
    case SEND_POST_DATA:
        status->rxBytesWaiting = status->connected = 0;
        status->error = NETWORK_ERROR_SUCCESS;
        return false;
    case GET_HEADERS:
        if (resultCode == 0)
            http_transaction();
        status->rxBytesWaiting = (returned_headers.empty() ? 0 : returned_headers[returned_header_cursor].size());
        status->connected = resultCode > 0; // returns 1 if transaction has happened.
        status->error = error;
        return false;
    default:
        return true;
    }
}

bool NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    Debug_printf("NetworkProtocolHTTP::read_file_handle(%p,%u)\n", buf, len);
    switch (httpChannelMode)
    {
    case DATA:
        return read_file_handle_data(buf, len);
    case COLLECT_HEADERS:
    case SET_HEADERS:
    case SEND_POST_DATA:
        error = NETWORK_ERROR_WRITE_ONLY;
        return true;
    case GET_HEADERS:
        return read_file_handle_header(buf, len);
    default:
        return true;
    }
}

bool NetworkProtocolHTTP::read_file_handle_header(uint8_t *buf, unsigned short len)
{
    memcpy(buf, returned_headers[returned_header_cursor++].data(), len);
    return returned_header_cursor > returned_headers.size();
}

bool NetworkProtocolHTTP::read_file_handle_data(uint8_t *buf, unsigned short len)
{
    int actual_len;

    Debug_printf("NetworkProtocolHTTP::read_file_handle_data()\n");

    if (resultCode == 0)
        http_transaction();

    actual_len = client->read(buf, len);

    return len != actual_len;
}

bool NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len)
{
    bool err = false;

    Debug_printf("NetworkProtocolHTTP::read_dir_entry(%p,%u)\n", buf, len);

    // TODO: Get directory attribute.

    if (dirEntryCursor != webDAV.entries.end())
    {
        fileSize = atoi(dirEntryCursor->fileSize.c_str());
        strcpy(buf, dirEntryCursor->filename.c_str());
        dirEntryCursor++;
    }
    else
    {
        // EOF
        error = 136;
        err = true;
    }

    Debug_printf("Returning: %s, %u\n", buf, fileSize);

    return err;
}

bool NetworkProtocolHTTP::close_file_handle()
{
    Debug_printf("NetworkProtocolHTTP::close_file_Handle()\n");

    if (client != nullptr)
    {
        if (httpOpenMode == PUT)
            http_transaction();
        client->close();
        fserror_to_error();
    }

    return (error == 1 ? false : true);
}

bool NetworkProtocolHTTP::close_dir_handle()
{
    Debug_printf("NetworkProtocolHTTP::close_dir_handle()\n");
    return false;
}

bool NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    Debug_printf("NetworkProtocolHTTP::write_file_handle(%p,%u)\n", buf, len);

    switch (httpChannelMode)
    {
    case DATA:
        return write_file_handle_data(buf, len);
    case COLLECT_HEADERS:
        return write_file_handle_get_header(buf, len);
    case SET_HEADERS:
        return write_file_handle_set_header(buf, len);
    case SEND_POST_DATA:
        return write_file_handle_send_post_data(buf, len);
    case GET_HEADERS:
        error = NETWORK_ERROR_READ_ONLY;
        return true;
    default:
        return true;
    }
}

bool NetworkProtocolHTTP::write_file_handle_get_header(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode == GET)
    {
        char *requestedHeader = (char *)malloc(len);

        memset(requestedHeader, 0, len);

        if (requestedHeader == nullptr)
        {
            Debug_printf("Could not allocate %u bytes for header\n", len);
            return true;
        }

        // move source buffer into requested header.
        memcpy(requestedHeader, buf, len);

        // Remove EOL, make NUL delimited.
        for (int i = 0; i < len; i++)
            if (requestedHeader[i] == 0x9B)
                requestedHeader[i] = 0x00;

        Debug_printf("collect_headers[%u,%u] = \"%s\"\n", collect_headers_count, len, requestedHeader);

        // Add result to header array.
        collect_headers[collect_headers_count++] = requestedHeader;
        return false;
    }
    else
    {
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }
}

bool NetworkProtocolHTTP::write_file_handle_set_header(uint8_t *buf, unsigned short len)
{
    string incomingHeader = string((char *)buf, len);
    size_t pos = incomingHeader.find('\x9b');

    // Erase ATASCII EOL if present
    if (pos != string::npos)
        incomingHeader.erase(pos);

    // Find delimiter
    pos = incomingHeader.find(":");

    if (pos == string::npos)
        return true;

    Debug_printf("NetworkProtocolHTTP::write_file_set_header(%s,%s)", incomingHeader.substr(0, pos).c_str(), incomingHeader.substr(pos + 2).c_str());

    client->set_header(incomingHeader.substr(0, pos).c_str(), incomingHeader.substr(pos + 2).c_str());
    return false;
}

bool NetworkProtocolHTTP::write_file_handle_send_post_data(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode != POST)
    {
        error = NETWORK_ERROR_INVALID_COMMAND;
        return true;
    }

    postData += string((char *)buf, len);
    return false;
}

bool NetworkProtocolHTTP::write_file_handle_data(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode != PUT)
    {
        error = NETWORK_ERROR_INVALID_COMMAND;
        return true;
    }

    postData += string((char *)buf, len);
    return false; // come back here later.
}

bool NetworkProtocolHTTP::stat()
{
    bool ret = false;
    return ret; // short circuit it for now.

    Debug_printf("NetworkProtocolHTTP::stat(%s)\n", opened_url->toString().c_str());

    if (aux1_open != 4) // only for READ FILE
        return false;   // We don't care.

    // Since we know client is active, we need to destroy it.
    delete client;

    // Temporarily use client to do the HEAD request
    client = new fnHttpClient();
    client->begin(opened_url->toString());
    resultCode = client->HEAD();
    fserror_to_error();

    if ((resultCode == 0) || (resultCode > 399))
        ret = true;
    else
    {
        // We got valid data, set filesize, then close and dispose of client.
        fileSize = client->available();

        client->close();
        delete client;

        // Recreate it for the rest of resolve()
        client = new fnHttpClient();
        ret = !client->begin(opened_url->toString());
        resultCode = 0; // so GET will actually happen.
    }

    return ret;
}

void NetworkProtocolHTTP::http_transaction()
{
    if ((aux1_open != 4) && (aux1_open != 8) && (collect_headers_count > 0))
    {
        Debug_printf("CALLING COLLECT HEADERS!\n");
        client->collect_headers((const char **)collect_headers, collect_headers_count);
    }

    switch (httpOpenMode)
    {
    case GET:
        resultCode = client->GET();
        break;
    case POST:
        resultCode = client->POST(postData.c_str(), postData.size());
        break;
    case PUT:
        resultCode = client->PUT(postData.c_str(), postData.size());
        break;
    }

    if ((aux1_open != 4) && (aux1_open != 8) && (collect_headers_count > 0))
    {
        Debug_printf("Header count %u\n", client->get_header_count());

        for (int i = 0; i < client->get_header_count(); i++)
        {
            returned_headers.push_back(string(client->get_header(i) + "\x9b"));
            Debug_printf("returned_headers[%u]=\"%s\"\n", i, returned_headers[i].c_str());
        }
    }

    fileSize = bodySize = client->available();
}

bool NetworkProtocolHTTP::parseDir(char *buf, unsigned short len)
{
    XML_Parser p = XML_ParserCreate(NULL);
    XML_Status xs;
    bool err = false;

    if (p == nullptr)
    {
        Debug_printf("NetworkProtocolHTTP::parseDir(%p,%u) - could not create expat parser. Aborting.\n");
        return true;
    }

    // Put PROPFIND data to debug console
    Debug_printf("PROPFIND DATA:\n\n%s\n",buf);

    // Set everything up
    XML_SetUserData(p, &webDAV);
    XML_SetElementHandler(p, Start<WebDAV>, End<WebDAV>);
    XML_SetCharacterDataHandler(p, Char<WebDAV>);

    // And parse the damned buffer
    xs = XML_Parse(p, buf, len, true);

    if (xs == XML_STATUS_ERROR)
    {
        Debug_printf("DAV response XML Parse Error! msg: %s line: %lu\n", XML_ErrorString(XML_GetErrorCode(p)), XML_GetCurrentLineNumber(p));
    }

    if (p != nullptr)
        XML_ParserFree(p);

    return err;
}

bool NetworkProtocolHTTP::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::rename(url, cmdFrame) == true)
        return true;

    url->path = url->path.substr(0,url->path.find(","));

    mount(url);

    resultCode = client->MOVE(destFilename.c_str(),true);
    fserror_to_error();

    umount();

    return resultCode > 399;
}

bool NetworkProtocolHTTP::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    resultCode = client->DELETE();
    fserror_to_error();

    umount();

    return resultCode > 399;
}

bool NetworkProtocolHTTP::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolHTTP::mkdir(%s,%s)", url->hostName, url->path);

    mount(url);

    resultCode = client->MKCOL();

    umount();

    return resultCode > 399;
}

bool NetworkProtocolHTTP::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return del(url, cmdFrame);
}