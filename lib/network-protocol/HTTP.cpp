/**
 * NetworkProtocolHTTP
 * 
 * Implementation
 */

#include <algorithm>
#include "HTTP.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolHTTP::NetworkProtocolHTTP(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
}

NetworkProtocolHTTP::~NetworkProtocolHTTP()
{
}

bool NetworkProtocolHTTP::open_file_handle()
{
    switch (aux1_open)
    {
    case 4:
        httpMode = GET;
        break;
    case 8:
        httpMode = PUT;
        fpPUT = fnSystem.make_tempfile(cPUT);
        break;
    case 9:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
    case 12:
        httpMode = POST;
        break;
    }

    fix_scheme();

    return !client.begin(url_to_string());
}

bool NetworkProtocolHTTP::open_dir_handle()
{
    return true;
}

bool NetworkProtocolHTTP::mount(string hostName, string path)
{
    // not used
    return false;
}

bool NetworkProtocolHTTP::umount()
{
    // not used
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

void NetworkProtocolHTTP::start_connection()
{
    // Start HTTP transfer, if not started.
    if (verbCompleted == false)
    {
        switch (httpMode)
        {
        case GET:
            resultCode = client.GET();
            verbCompleted=true;
            break;
        case POST:
        case PUT:
        case PROPFIND:
            error = NETWORK_ERROR_NOT_IMPLEMENTED;
            break;
        }
    }
}

bool NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    bool ret = true;

    switch (protocolMode)
    {
    case DATA:
        ret = read_file_handle_data(buf, len);
        break;
    case HEADERS:
        break;
    }
    return ret;
}

bool NetworkProtocolHTTP::read_file_handle_data(uint8_t *buf, unsigned short len)
{
    bool ret = true;

    start_connection();
    ret = (client.read(buf, len) != len);

    fserror_to_error();
    return ret;
}

bool NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolHTTP::close_file_handle()
{
    client.close();
    return false;
}

bool NetworkProtocolHTTP::close_dir_handle()
{
    client.close();
    return true;
}

bool NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    return true;
}

void NetworkProtocolHTTP::get_file_size(string url)
{
    const char *content_length_header[] = {"Content-Length"};
    client.begin(url);
    client.collect_headers(content_length_header, 1);
    resultCode = client.HEAD();
    fserror_to_error();
    fileSize = atoi(client.get_header(0).c_str());
    client.close();
    Debug_printf("NetworkProtocolHTTP::get_file_size(%s): %d",url.c_str(),fileSize);
}

bool NetworkProtocolHTTP::stat(string path)
{

    return false;
}

uint8_t NetworkProtocolHTTP::special_inquiry(uint8_t cmd)
{
    return 0xff;
}

bool NetworkProtocolHTTP::special_00(cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolHTTP::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::parse_dir(string s)
{
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser, &dav);
    XML_SetElementHandler(parser, Start<WebDAV>, End<WebDAV>);
    XML_SetCharacterDataHandler(parser, Char<WebDAV>);
    return XML_Parse(parser, s.c_str(), s.size(), true) == XML_STATUS_ERROR;
}

void NetworkProtocolHTTP::fix_scheme()
{
    std::transform(opened_url->scheme.begin(), opened_url->scheme.end(), opened_url->scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (opened_url->port.empty() && opened_url->scheme == "http")
        opened_url->port = "80";
    else if (opened_url->port.empty() && opened_url->scheme == "https")
        opened_url->port = "443";
}

string NetworkProtocolHTTP::url_to_string(string path)
{
    return opened_url->scheme + "://" + opened_url->hostName + ":" + opened_url->port + path + (opened_url->query.empty() ? "" : ("?") + opened_url->query);
}

string NetworkProtocolHTTP::url_to_string()
{
    return url_to_string(opened_url->path);
}