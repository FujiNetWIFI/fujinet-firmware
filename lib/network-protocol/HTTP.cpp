/**
 * HTTP implementation
 */

#include "HTTP.h"
#include "status_error_codes.h"

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
}

NetworkProtocolHTTP::~NetworkProtocolHTTP()
{
}

bool NetworkProtocolHTTP::open_file_handle()
{
    Debug_printf("NetworkProtocolHTTP::open_file_handle()\n");

    error = NETWORK_ERROR_SUCCESS;

    switch (aux1_open)
    {
    case 4:
        openMode = GET;
        break;
    case 8:
        openMode = PUT;
        break;
    case 12:
        openMode = POST;
        break;
    default:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    }

    return false;
}

bool NetworkProtocolHTTP::open_dir_handle()
{
    Debug_printf("NetworkProtocolHTTP::open_dir_handle()\n");
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

bool NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    Debug_printf("NetworkProtocolHTTP::read_file_handle(%p,%u)\n", buf, len);

    if (resultCode == 0)
        http_transaction();

    client->read(buf,len);

    fileSize = client->available();

    return false;
}

bool NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len)
{    
    Debug_printf("NetworkProtocolHTTP::read_dir_entry(%p,%u)\n", buf, len);
    return false;
}

bool NetworkProtocolHTTP::close_file_handle()
{
    Debug_printf("NetworkProtocolHTTP::close_file_Handle()\n");
    if (client != nullptr)
        client->close();
    return false;
}

bool NetworkProtocolHTTP::close_dir_handle()
{
    Debug_printf("NetworkProtocolHTTP::close_dir_handle()\n");
    return false;
}

bool NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    Debug_printf("NetworkProtocolHTTP::write_file_handle(%p,%u)\n", buf, len);
    return false;
}

bool NetworkProtocolHTTP::stat(string path)
{
    Debug_printf("NetworkProtocolHTTP::stat(%s)\n", path.c_str());
    return false;
}

void NetworkProtocolHTTP::http_transaction()
{
    switch (openMode)
    {
        case GET:
            resultCode = client->GET();
            break;
        case POST:
            // resultCode = client->POST();
            break;
        case PUT:
            // resultCode = client->PUT();
            break;
    }

    fileSize = client->available();
}