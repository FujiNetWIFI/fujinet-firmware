/**
 * HTTP implementation
 */

#define VERBOSE_HTTP 1
#define VERBOSE_PROTOCOL 1

#include <cstring>

#include "HTTP.h"

#include "../../include/debug.h"

#include "status_error_codes.h"
#include "utils.h"
#include "string_utils.h"
#include "compat_string.h"

#include <vector>

/**
 Modes and the N: HTTP Adapter:

Aux1 values
===========

4 = GET, with filename translation, URL encoding.
5 = DELETE, no headers
6 = PROPFIND, WebDAV directory
8 = PUT, write data to server, XIO used to toggle headers to get versus data write
9 = DELETE, with headers
12 = GET, pure and unmolested
13 = POST, write sends post data to server, read grabs response, XIO used to change write behavior, toggle headers to get or headers to set.
14 = PUT, write sends post data to server, read grabs response, XIO used to change write behavior, toggle headers to get or headers to set.
DELETE, MKCOL, RMCOL, COPY, MOVE, are all handled via idempotent XIO commands.
DELETE can be done via special/XIO if you do not want to handle the response, otherwise use aux1=5/9 with normal open/read.
*/

NetworkProtocolHTTP::NetworkProtocolHTTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    fileSize = 0;
    resultCode = 0;
    // collect_headers_count = 0;
    returned_header_cursor = 0;
    httpChannelMode = DATA;
}

NetworkProtocolHTTP::~NetworkProtocolHTTP()
{
    if (client)
        delete(client);
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

netProtoErr_t NetworkProtocolHTTP::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'M':
        return special_set_channel_mode(cmdFrame);
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

netProtoErr_t NetworkProtocolHTTP::special_set_channel_mode(cmdFrame_t *cmdFrame)
{
    netProtoErr_t err = NETPROTO_ERR_NONE;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::special_set_channel_mode(%u)\r\n", httpChannelMode);
#endif

    receiveBuffer->clear();
    transmitBuffer->clear();

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
        returned_header_cursor = 0;
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
        err = NETPROTO_ERR_UNSPECIFIED;
    }

    return err;
}

netProtoErr_t NetworkProtocolHTTP::open_file_handle()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printv("NetworkProtocolHTTP::open_file_handle() aux1[%d]\r\n", aux1_open);
#endif
    error = NETWORK_ERROR_SUCCESS;

    switch (aux1_open)
    {
    case PROTOCOL_OPEN_READ:        // GET with no headers, filename resolve
    case PROTOCOL_OPEN_READWRITE:   // GET with ability to set headers, no filename resolve.
        httpOpenMode = GET;
        break;
    case PROTOCOL_OPEN_WRITE:       // WRITE, filename resolve, ignored if not found.
        httpOpenMode = PUT;
        break;
    case PROTOCOL_OPEN_HTTP_DELETE: // DELETE with no headers
    case PROTOCOL_OPEN_APPEND:      // DELETE with ability to set headers
        httpOpenMode = DELETE;
        break;
    case PROTOCOL_OPEN_HTTP_POST:   // POST can set headers, also no filename resolve
    case PROTOCOL_OPEN_HTTP_PUT:    // PUT with ability to set headers, no filename resolve
        httpOpenMode = POST;
        break;
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // This is set IF we came back through here via resolve().
    if (resultCode > 399)
    {
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::open_dir_handle()
{
    int len, actual_len;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::open_dir_handle()\r\n");
#endif

    if (client != nullptr)
    {
        delete client;
        client = new HTTP_CLIENT_CLASS();
        client->begin(opened_url->url);
    }

    // client->begin already called in mount()
    resultCode = client->PROPFIND(HTTP_CLIENT_CLASS::webdav_depth::DEPTH_1,
    "<?xml version=\"1.0\"?>\r\n"
    "<D:propfind xmlns:D=\"DAV:\">\r\n"
    "<D:prop>\r\n<D:displayname />\r\n<D:getcontentlength /><D:resourcetype /></D:prop>\r\n"
    "</D:propfind>\r\n");

    // If Method not allowed, try GET.
    if (resultCode == 405 || resultCode == 408)
    {
        httpOpenMode = GET;
        http_transaction();
        return NETPROTO_ERR_NONE;
    }

    if (resultCode > 399)
    {
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Could not do PROPFIND. Result code %u\r\n", resultCode);
#endif
        fserror_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // Setup XML WebDAV parser
    if (webDAV.begin_parser())
    {
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Failed to setup parser.\r\n");
#endif
        error = NETWORK_ERROR_GENERAL;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    std::vector<uint8_t> buf;

    // Process all response chunks
    while ( !client->is_transaction_done() || client->available() > 0)
    {
        len = client->available();
        if (len > 0)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_printf("data available %d ...\n", len);
#endif
            // increase chunk buffer if necessary
            if (len >= buf.size())
                buf.resize(len+1); // +1 for '\0'

            // Grab the buffer
            actual_len = client->read(buf.data(), len);

            if (actual_len != len)
            {
#ifdef VERBOSE_PROTOCOL
                Debug_printf("Expected %d bytes, actually got %d bytes.\r\n", len, actual_len);
#endif
                error = NETWORK_ERROR_GENERAL;
                break;
            }
            buf[len] = '\0'; // make buffer C string compatible for Debug_printf()

            // Parse the buffer
            if (webDAV.parse((char *)buf.data(), len, false))
            {
#ifdef VERBOSE_PROTOCOL
                Debug_printf("Could not parse buffer, returning 144\r\n");
#endif
                error = NETWORK_ERROR_GENERAL;
                break;
            }
        }
        else if (len == 0)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_println("waiting for data\r\n");
#endif
#ifdef ESP_PLATFORM
            vTaskDelay(100 / portTICK_PERIOD_MS); // TBD !!!
#endif
        }
        else // if (len < 0)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_println("ERROR: negative length returned from client->available()\r\n");
#endif
            error = NETWORK_ERROR_GENERAL;
            break;
        }
    }

    if (error != NETWORK_ERROR_SUCCESS)
    {
#ifdef VERBOSE_PROTOCOL
        Debug_printf("NetworkProtocolHTTP::open_dir_handle() - error %u\r\n", error);
#endif
        webDAV.end_parser(true); // release parser resources + clear collected entries
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // finish parsing (not sure if this is necessary)
    webDAV.parse(nullptr, 0, true);

    // Release parser resources (keep directory entries)
    webDAV.end_parser();

    // Scoot to beginning of directory entries.
    dirEntryCursor = webDAV.rewind();

    if (client != nullptr)
    {
        delete client;
        client = new HTTP_CLIENT_CLASS();
        client->begin(opened_url->url);
    }

    // Directory parsed, ready to be returned by read_dir_entry()
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::mount(PeoplesUrlParser *url)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::mount(%s)\r\n", url->url.c_str());
#endif

    // fix scheme because esp-idf hates uppercase for some #()$@ reason.
    if (url->scheme == "HTTP")
    {
        url->scheme = "http";
        url->rebuildUrl();
    }
    else if (url->scheme == "HTTPS")
    {
        url->scheme = "https";
        url->rebuildUrl();
    }

    if (client)
        delete client;

    client = new HTTP_CLIENT_CLASS();

    // fileSize = 65535;

    if (aux1_open == 6)
    {
        util_replaceAll(url->path, "*.*", "");
        url->rebuildUrl();
    }

    if (aux1_open == 4 || aux1_open == 8)
    {
        // We are opening a file, URL encode the path.
        std::string encoded = mstr::urlEncode(url->path);
        url->path = encoded;
        url->rebuildUrl();
    }

    return client->begin(url->url) ? NETPROTO_ERR_NONE : NETPROTO_ERR_UNSPECIFIED;
}

netProtoErr_t NetworkProtocolHTTP::umount()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::umount()\r\n");
#endif

    if (client == nullptr)
        return NETPROTO_ERR_NONE;

    delete client;
    client = nullptr;

    return NETPROTO_ERR_NONE;
}

void NetworkProtocolHTTP::fserror_to_error()
{
    switch (resultCode)
    {
    case 901: // Fake HTTP status code indicating connection error
        error = NETWORK_ERROR_NOT_CONNECTED;
        break;
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
        error = NETWORK_ERROR_CLIENT_GENERAL;
        break;
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
        error = NETWORK_ERROR_SERVER_GENERAL;
        break;
    default:
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

netProtoErr_t NetworkProtocolHTTP::status_file(NetworkStatus *status)
{
    // if (fromInterrupt == false)
    //     Debug_printf("Channel mode is %u\r\n", httpChannelMode);

    if (client == nullptr) {
        status->rxBytesWaiting = 0;
        status->connected = 0;
        status->error = NETWORK_ERROR_GENERAL;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    switch (httpChannelMode)
    {
    case DATA:
    {
        if (!fromInterrupt && resultCode == 0 && aux1_open != OPEN_MODE_HTTP_PUT_H)
        {
#ifdef VERBOSE_PROTOCOL
            Debug_printf("calling http_transaction\r\n");
#endif
            http_transaction();
        }
        auto available = client->available();
        status->rxBytesWaiting = available > 65535 ? 65535 : available;
        status->connected = client->is_transaction_done() ? 0 : 1;

        if (available == 0 && client->is_transaction_done() && error == NETWORK_ERROR_SUCCESS)
            status->error = NETWORK_ERROR_END_OF_FILE;
        else
            status->error = error;
        // Debug_printf("NetworkProtocolHTTP::status_file DATA, available: %d, s.rxBW: %d, s.conn: %d, s.err: %d\r\n", available, status->rxBytesWaiting, status->connected, status->error);
        return NETPROTO_ERR_NONE;
    }
    case SET_HEADERS:
    case COLLECT_HEADERS:
    case SEND_POST_DATA:
        status->rxBytesWaiting = status->connected = 0;
        status->error = NETWORK_ERROR_SUCCESS;
        // Debug_printf("NetworkProtocolHTTP::status_file SH/CH/SPD, s.rxBW: %d, s.conn: %d, s.err: %d\r\n", status->rxBytesWaiting, status->connected, status->error);
        return NETPROTO_ERR_NONE;
    case GET_HEADERS:
        if (resultCode == 0)
            http_transaction();
        status->rxBytesWaiting = (returned_header_cursor < collect_headers.size() ? returned_headers[returned_header_cursor].size() : 0);
        status->connected = 0; // so that we always ask in this mode.
        status->error = returned_header_cursor == collect_headers.size() && error == NETWORK_ERROR_SUCCESS ? NETWORK_ERROR_END_OF_FILE : error;
        // Debug_printf("NetworkProtocolHTTP::status_file GH, s.rxBW: %d, s.conn: %d, s.err: %d\r\n", status->rxBytesWaiting, status->connected, status->error);
        return NETPROTO_ERR_NONE;
    default:
        Debug_printf("ERROR: Unknown httpChannelMode: %d\r\n", httpChannelMode);
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

netProtoErr_t NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::read_file_handle(%p,%u)\r\n", buf, len);
#endif
    switch (httpChannelMode)
    {
    case DATA:
        return read_file_handle_data(buf, len);
    case COLLECT_HEADERS:
    case SET_HEADERS:
    case SEND_POST_DATA:
        error = NETWORK_ERROR_WRITE_ONLY;
        return NETPROTO_ERR_UNSPECIFIED;
    case GET_HEADERS:
        return read_file_handle_header(buf, len);
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

netProtoErr_t NetworkProtocolHTTP::read_file_handle_header(uint8_t *buf, unsigned short len)
{
    memcpy(buf, returned_headers[returned_header_cursor++].data(), len);
    return returned_header_cursor > returned_headers.size()
        ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::read_file_handle_data(uint8_t *buf, unsigned short len)
{
    int actual_len;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::read_file_handle_data()\r\n");
#endif

    if (resultCode == 0)
        http_transaction();

    actual_len = client->read(buf, len);

    return len != actual_len ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len)
{
    netProtoErr_t err = NETPROTO_ERR_NONE;

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::read_dir_entry(%p,%u)\r\n", buf, len);
#endif

    if (dirEntryCursor != webDAV.entries.end())
    {
        strlcpy(buf, dirEntryCursor->filename.c_str(), len);
        fileSize = atoi(dirEntryCursor->fileSize.c_str());
        is_directory = dirEntryCursor->isDir;
        ++dirEntryCursor;
#ifdef VERBOSE_PROTOCOL
        Debug_printf("Returning: %s, %u, %s\r\n", buf, fileSize, is_directory ? "DIR" : "FILE");
#endif
    }
    else
    {
        // EOF
        error = NETWORK_ERROR_END_OF_FILE;
        err = NETPROTO_ERR_UNSPECIFIED;
    }

    return err;
}

netProtoErr_t NetworkProtocolHTTP::close_file_handle()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::close_file_Handle()\r\n");
#endif

    if (client != nullptr)
    {
        if (httpOpenMode == PUT || aux1_open == OPEN_MODE_HTTP_PUT_H)
            http_transaction();
        client->close();
        fserror_to_error();
    }

    return (error == 1 ? NETPROTO_ERR_NONE : NETPROTO_ERR_UNSPECIFIED);
}

netProtoErr_t NetworkProtocolHTTP::close_dir_handle()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::close_dir_handle()\r\n");
#endif
    webDAV.clear(); // release directory entries
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::write_file_handle(%p,%u)\r\n", buf, len);
#endif

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
        return NETPROTO_ERR_UNSPECIFIED;
    default:
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

netProtoErr_t NetworkProtocolHTTP::write_file_handle_get_header(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode != GET)
    {
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    if (len > 0) {
        unsigned char lastChar = buf[len - 1];
        if (lastChar == 0x9B || lastChar == '\r' || lastChar == '\n') {
            len--;
        }
    }
    std::string requestedHeader(reinterpret_cast<const char*>(buf), len);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("collect_headers[%lu,%u] = \"%s\"\r\n", (unsigned long)collect_headers.size(), len, requestedHeader.c_str());
#endif

    // Add result to header vector.
    collect_headers.push_back(std::move(requestedHeader)); // Use std::move to avoid copying the string
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::write_file_handle_set_header(uint8_t *buf, unsigned short len)
{
    std::string incomingHeader = std::string((char *)buf, len);
    size_t pos = incomingHeader.find('\x9b');

    // Erase ATASCII EOL if present
    if (pos != std::string::npos)
        incomingHeader.erase(pos);

    // Find delimiter
    pos = incomingHeader.find(":");

    if (pos == std::string::npos)
        return NETPROTO_ERR_UNSPECIFIED;

#ifdef ESP_PLATFORM
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::write_file_set_header(%s,%s)\r\n", incomingHeader.substr(0, pos).c_str(), incomingHeader.substr(pos + 2).c_str());
#endif

    client->set_header(incomingHeader.substr(0, pos).c_str(), incomingHeader.substr(pos + 2).c_str());
#else // TODO merge
    std::string key(incomingHeader.substr(0, pos));
    std::string val(incomingHeader.substr(pos + 2));
    util_string_trim(key);
    util_string_trim(val);

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::write_file_set_header(%s,%s)\r\n", key.c_str(), val.c_str());
#endif

    client->set_header(key.c_str(), val.c_str());
#endif
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::write_file_handle_send_post_data(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode != POST)
    {
        error = NETWORK_ERROR_INVALID_COMMAND;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    postData += std::string((char *)buf, len);
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::write_file_handle_data(uint8_t *buf, unsigned short len)
{
    if (httpOpenMode == PUT || aux1_open == OPEN_MODE_HTTP_PUT_H)
    {
        postData += std::string((char *)buf, len);
        return NETPROTO_ERR_NONE; // come back here later.
    }

    error = NETWORK_ERROR_INVALID_COMMAND;
    return NETPROTO_ERR_UNSPECIFIED;
}

netProtoErr_t NetworkProtocolHTTP::stat()
{
    netProtoErr_t ret = NETPROTO_ERR_NONE;
    return ret; // short circuit it for now.

#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::stat(%s)\r\n", opened_url->url.c_str());
#endif

    if (aux1_open != 4) // only for READ FILE
        return NETPROTO_ERR_NONE;   // We don't care.

    // Since we know client is active, we need to destroy it.
    delete client;

    // Temporarily use client to do the HEAD request
    client = new HTTP_CLIENT_CLASS();
    client->begin(opened_url->url);
    resultCode = client->HEAD();
    fserror_to_error();

    if ((resultCode == 0) || (resultCode > 399))
        ret = NETPROTO_ERR_UNSPECIFIED;
    else
    {
        // We got valid data, set filesize, then close and dispose of client.
        fileSize = client->available();

        client->close();
        delete client;

        // Recreate it for the rest of resolve()
        client = new HTTP_CLIENT_CLASS();
        ret = client->begin(opened_url->url) ? NETPROTO_ERR_NONE : NETPROTO_ERR_UNSPECIFIED;
        resultCode = 0; // so GET will actually happen.
    }

    return ret;
}

void NetworkProtocolHTTP::http_transaction()
{
    if ((aux1_open != 4) && (aux1_open != 8) && !collect_headers.empty())
    {
        client->create_empty_stored_headers(collect_headers);
    }

    switch (httpOpenMode)
    {
    case GET:
        resultCode = client->GET();
        break;
    case POST:
        if (aux1_open == OPEN_MODE_HTTP_PUT_H)
            resultCode = client->PUT(postData.c_str(), postData.size());
        else
            resultCode = client->POST(postData.c_str(), postData.size());
        break;
    case PUT:
        resultCode = client->PUT(postData.c_str(), postData.size());
        break;
    case DELETE:
        resultCode = client->DELETE();
        break;
    }

    // the appropriate headers to be collected should have now been done, so let's put their values into returned_headers
    if ((aux1_open != 4) && (aux1_open != 8) && (!collect_headers.empty()))
    {
#ifdef VERBOSE_PROTOCOL
        Debug_printf("setting returned_headers (count =%u)\r\n", client->get_header_count());
#endif

        for (const auto& header_pair : client->get_stored_headers()) {
            std::string header = header_pair.second + "\x9b"; // TODO: can we use platform specific value rather than ATARI default?
            returned_headers.push_back(header);
        }
    }

    fserror_to_error();
    fileSize = bodySize = client->available();
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::http_transaction() done, resultCode=%d, fileSize=%u\r\n", resultCode, fileSize);
#endif
}

netProtoErr_t NetworkProtocolHTTP::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::rename(url, cmdFrame) == true)
        return NETPROTO_ERR_UNSPECIFIED;

    url->path = url->path.substr(0, url->path.find(","));

    mount(url);

    resultCode = client->MOVE(destFilename.c_str(), true);
    fserror_to_error();

    umount();

    return resultCode > 399 ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::del(%s,%s)", url->host.c_str(), url->path.c_str());
#endif
    mount(url);

    resultCode = client->DELETE();
    fserror_to_error();

    umount();

    return resultCode > 399 ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocolHTTP::mkdir(%s,%s)", url->host.c_str(), url->path.c_str());
#endif

    mount(url);

    resultCode = client->MKCOL();

    umount();

    return resultCode > 399 ? NETPROTO_ERR_UNSPECIFIED : NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolHTTP::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return del(url, cmdFrame);
}
