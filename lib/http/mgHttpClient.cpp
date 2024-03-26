#ifndef ESP_PLATFORM

// TODO: Figure out why time-outs against bad addresses seem to take about 18s no matter
// what we set the timeout value to.

#include <cstdlib>
#include <string.h>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "fnSystem.h"
#include "utils.h"
#include "mgHttpClient.h"

#include "../../include/debug.h"

#include "mongoose.h"

#define HTTPCLIENT_WAIT_FOR_CONSUMER_TASK 20000 // 20s
#define HTTPCLIENT_WAIT_FOR_HTTP_TASK 20000     // 20s

#define DEFAULT_HTTP_BUF_SIZE (512)

const char *webdav_depths[] = {"0", "1", "infinity"};

mgHttpClient::mgHttpClient()
{
    _buffer = nullptr;
}

// Close connection, destroy any resoruces
mgHttpClient::~mgHttpClient()
{
    close();

    if (_handle != nullptr)
        mg_mgr_free(_handle);

    if (_buffer != nullptr) {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient buffer free\n");
#endif
        free(_buffer);
        _buffer = nullptr;
    }
}

// Start an HTTP client session to the given URL
bool mgHttpClient::begin(std::string url)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient::begin \"%s\"\n", url.c_str());
#endif

    _max_redirects = 10;

    _handle = new(struct mg_mgr);
    if (_handle == nullptr)
        return false;

    _url = url;
    mg_mgr_init(_handle);
    return true;
}

int mgHttpClient::available()
{
    int result = 0;

    if (_handle != nullptr) {
        int len = _buffer_len;
        if (len - _buffer_total_read >= 0)
            result = len - _buffer_total_read;
    }
    return result;
}

/*
 Reads HTTP response data
 Return value is bytes stored in buffer or -1 on error
 Buffer will NOT be zero-terminated
 Return value >= 0 but less than dest_bufflen indicates end of data
*/
int mgHttpClient::read(uint8_t *dest_buffer, int dest_bufflen)
{
    if (_handle == nullptr || dest_buffer == nullptr)
        return -1;

    int bytes_left;
    int bytes_to_copy;

    int bytes_copied = 0;

    // Start by using our own buffer if there's still data there
    // if (_buffer_pos > 0 && _buffer_pos < _buffer_len)
    if (_buffer_len > 0 && _buffer_pos < _buffer_len)
    {
        bytes_left = _buffer_len - _buffer_pos;
        bytes_to_copy = dest_bufflen > bytes_left ? bytes_left : dest_bufflen;

        //Debug_printf("::read from buffer %d\n", bytes_to_copy);
        memcpy(dest_buffer, _buffer + _buffer_pos, bytes_to_copy);
        _buffer_pos += bytes_to_copy;
        _buffer_total_read += bytes_to_copy;

        bytes_copied = bytes_to_copy;
    }

    return bytes_copied;

}

void mgHttpClient::_flush_response()
{
}

// Close connection, but keep request resources
void mgHttpClient::close()
{
    Debug_println("mgHttpClient::close");
    _stored_headers.clear();
    _request_headers.clear();
}

/*
 Typical event order:
 
 HTTP_EVENT_HANDLER_ON_CONNECTED
 HTTP_EVENT_HEADERS_SENT
 HTTP_EVENT_ON_HEADER - once for each header received with header_key and header_value set
 HTTP_EVENT_ON_DATA - multiple times with data and datalen set up to BUFFER size
 HTTP_EVENT_ON_FINISH - value is returned to esp_http_client_perform() after this
 HTTP_EVENT_DISCONNECTED

 The return value is discarded.
*/
void mgHttpClient::_httpevent_handler(struct mg_connection *c, int ev, void *ev_data)
{
    // // Our user_data should be a pointer to our mgHttpClient object
    mgHttpClient *client = (mgHttpClient *)c->fn_data;
    bool progress = true;
    
    switch (ev)
    {
    case MG_EV_CONNECT:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Connected\n");
#endif
        const char *url = client->_url.c_str();
        struct mg_str host = mg_url_host(url);
        // If url is https://, tell client connection to use TLS
        if (mg_url_is_ssl(url))
        {
            struct mg_str key_data = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
            struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY                
            opts.ca.ptr = nullptr; // disable certificate checking 
#else
            opts.ca.ptr = "data/ca.pem";

            // this is how to load the files rather than refer to them by name (for BUILT_IN tls)
            // opts.ca = mg_file_read(&mg_fs_posix, "data/ca.pem");
            // opts.cert = mg_file_read(&mg_fs_posix, "tls/cert.pem");
            // opts.key = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
#endif
            opts.name = host;
            mg_tls_init(c, &opts);
        }

        // reset response status code
        client->_status_code = -1;

        // get authentication from url, if any provided
        if (mg_url_user(url).len != 0)
        {
            struct mg_str u = mg_url_user(url);
            struct mg_str p = mg_url_pass(url);
            client->_username = std::string(u.ptr, u.len);
            client->_password = std::string(p.ptr, p.len);
        }

        // Send request
        switch(client->_method)
        {
            case HTTP_GET:
            {
                mg_printf(c, "GET %s HTTP/1.0\r\n"
                                "Host: %.*s\r\n",
                                mg_url_uri(url), (int)host.len, host.ptr);
                // send auth header
                if (!client->_username.empty())
                    mg_http_bauth(c, client->_username.c_str(), client->_password.c_str());
                // send request headers
                for (const auto& rh: client->_request_headers)
                    mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
                mg_printf(c, "\r\n");
                break;
            }
            case HTTP_PUT:
            case HTTP_POST:
            {
                mg_printf(c, "%s %s HTTP/1.0\r\n"
                                "Host: %.*s\r\n",
                                (client->_method == HTTP_PUT) ? "PUT" : "POST",
                                mg_url_uri(url), (int)host.len, host.ptr);
                // send auth header
                if (!client->_username.empty())
                    mg_http_bauth(c, client->_username.c_str(), client->_password.c_str());
                // set Content-Type if not set
                header_map_t::iterator it = client->_request_headers.find("Content-Type");
                if (it == client->_request_headers.end())
                    client->set_header("Content-Type", "application/octet-stream");
                // send request headers
                for (const auto& rh: client->_request_headers)
                    mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
#ifdef VERBOSE_HTTP
                Debug_println("Custom headers");
                for (const auto& rh: client->_request_headers)
                    Debug_printf("  %s: %s\n", rh.first.c_str(), rh.second.c_str());
#endif
                mg_printf(c, "Content-Length: %d\r\n", client->_post_datalen);
                mg_printf(c, "\r\n");
                mg_send(c, client->_post_data, client->_post_datalen);
                break;
            }
            case HTTP_DELETE:
            {
                mg_printf(c, "DELETE %s HTTP/1.0\r\n"
                                "Host: %.*s\r\n",
                                mg_url_uri(url), (int)host.len, host.ptr);
                // send auth header
                if (!client->_username.empty())
                    mg_http_bauth(c, client->_username.c_str(), client->_password.c_str());
                // send request headers
                for (const auto& rh: client->_request_headers)
                    mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
                mg_printf(c, "\r\n");
                break;

            }
            default:
            {
#ifdef VERBOSE_HTTP
                Debug_printf("mgHttpClient: method %d is not implemented\n", client->_method);
#endif
            }
        }
        break;
    } // MG_EV_CONNECT

    case MG_EV_HTTP_MSG:
    {
        // Response received, send it to host
        struct mg_http_message hm;
        memcpy(&hm, (struct mg_http_message *) ev_data, sizeof(hm));

#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: HTTP response\n");
        Debug_printf("  Status: %.*s\n", (int)hm.uri.len, hm.uri.ptr);
        Debug_printf("  Received: %ld\n", (unsigned long)hm.message.len);
        Debug_printf("  Body: %ld bytes\n", (unsigned long)hm.body.len);
#endif

        // get response status code and content length
        client->_status_code = std::stoi(std::string(hm.uri.ptr, hm.uri.len));
        client->_content_length = (int)hm.body.len;

        if (client->_status_code == 301 || client->_status_code == 302)
        {
            // remember Location on redirect response
            struct mg_str *loc = mg_http_get_header(&hm, "Location");
            if (loc != nullptr)
                client->_location = std::string(loc->ptr, loc->len);
        }

        // get response headers client is interested in
        size_t max_headers = sizeof(hm.headers) / sizeof(hm.headers[0]);
        for (int i = 0; i < max_headers && hm.headers[i].name.len > 0; i++) 
        {
            // Check to see if we should store this response header
            if (client->_stored_headers.size() <= 0)
                break;

            struct mg_str *name = &hm.headers[i].name;
            struct mg_str *value = &hm.headers[i].value;
            std::string hkey(std::string(name->ptr, name->len));
            header_map_t::iterator it = client->_stored_headers.find(hkey);
            if (it != client->_stored_headers.end())
            {
                std::string hval(std::string(value->ptr, value->len));
                it->second = hval;
            }
        }

        // allocate buffer for received data
        // realloc == malloc if first param is NULL
        client->_buffer = (char *)realloc(client->_buffer, hm.body.len);

        // copy received data into buffer
        client->_buffer_pos = 0;
        if (client->_buffer != nullptr) {
            client->_buffer_len = hm.body.len;
            memcpy(client->_buffer, hm.body.ptr, client->_buffer_len);
        }
        else {
            client->_buffer_len = 0;
            if (hm.body.len != 0) {
                Debug_printf("mgHttpClient ERROR: buffer was not allocated for received data.");
            }
        }

        c->is_closing = 1;          // Tell mongoose to close this connection as it's completed
        c->recv.len = 0;
        client->_processed = true;  // Tell event loop to stop
        break;
    }

    case MG_EV_READ:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Data received\n");
#endif
        break;
    }

    case MG_EV_TLS_HS:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: TLS Handshake succeeded\n");
#endif
        break;
    }

    case MG_EV_HTTP_HDRS:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: HTTP Headers received\n");
#endif
        break;
    }
    case MG_EV_OPEN:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Open received\n");
#endif
        break;
    }

    case MG_EV_WRITE:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Data written\n");
#endif
        break;
    }
    
    case MG_EV_CLOSE:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Connection closed\n");
#endif
        *(bool *) c->fn_data = true;
        break;
    }
    
    case MG_EV_RESOLVE:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Host name resolved\n");
#endif
        break;
    }
    
    case MG_EV_ERROR:
    {
        Debug_printf("mgHttpClient: Error - %s\n", (const char*)ev_data);
        client->_processed = true;  // Error, tell event loop to stop
        client->_status_code = 901; // Fake HTTP status code to indicate connection error
        *(bool *) c->fn_data = true;
        break;
    }
    
    case MG_EV_POLL:
    {
        progress = false;
        break;
    }
    
    default:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient UNHANDLED EVENT: %d\n", ev);
#endif
        break;
    }
    }

    client->_progressed = progress;

}

/*
 Performs an HTTP transaction
 Outside of POST data, this can't write to the server.  However, it's the only way to
 retrieve response headers using the esp_http_client library, so we use it
 for all non-write methods: GET, HEAD, POST
*/
int mgHttpClient::_perform()
{
    Debug_printf("%08lx _perform\n", (unsigned long)fnSystem.millis());

    // We want to process the response body (if any)
    _ignore_response_body = false;

    _processed = false;
    _progressed = false;
    _redirect_count = 0;
    bool done = false;

    uint64_t ms_update = fnSystem.millis();
    // create client connection
    _perform_connect();

    while (!done)
    {
        while (!_processed)
        {
            mg_mgr_poll(_handle, 50);
            if (_progressed)
            {
                _progressed = false;
                ms_update = fnSystem.millis();
            }
            else 
            {
                // no progress, check for timeout
                if ((fnSystem.millis() - ms_update) > HTTP_TIMEOUT)
                    break;
            }
        }
        if (!_processed)
        {
            Debug_printf("Timed-out waiting for HTTP response\n");
            _status_code = 408; // 408 Request Timeout
        }
        // request/response processing done
        done = true;

        // check the response
        if (_status_code == 301 || _status_code == 302)
        {
            // handle HTTP redirect
            if (!_location.empty())
            {
                _redirect_count++;
                if (_redirect_count <= _max_redirects)
                {
                    Debug_printf("HTTP redirect (%d) to %s\n", _redirect_count, _location.c_str());
                    // new client connection
                    _url = _location;
                    _location.clear();
                    // need more processing
                    _processed = false;
                    done = false;
                    // create new connection
                    _perform_connect();
                }
                else
                {
                    Debug_printf("HTTP redirect (%d) over max allowed redirects (%d)!\n", _redirect_count, _max_redirects);
                }
            }
            else
            {
                Debug_printf("HTTP redirect (%d) without Location specified!\n", _redirect_count);
            }
        }
    }

    bool chunked = false; // TODO
    int status = _status_code;
    int length = _content_length;

    Debug_printf("%08lx _perform status = %d, length = %d, chunked = %d\n", (unsigned long)fnSystem.millis(), status, length, chunked ? 1 : 0);
    return status;
}

/*
 Resets variables and begins http transaction
 */
void mgHttpClient::_perform_connect()
{
    _status_code = -1;
    _content_length = 0;
    _buffer_len = 0;
    _buffer_total_read = 0;
    
    mg_http_connect(_handle, _url.c_str(), _httpevent_handler, this);  // Create client connection
}

int mgHttpClient::PUT(const char *put_data, int put_datalen)
{
    Debug_println("mgHttpClient::PUT");

    if (_handle == nullptr || put_data == nullptr || put_datalen < 1)
        return -1;

    _method = HTTP_PUT;
    _post_data = put_data;
    _post_datalen = put_datalen;

    return _perform();
}

int mgHttpClient::PROPFIND(webdav_depth depth, const char *properties_xml)
{
    Debug_println("mgHttpClient::PROPFIND");
    if (_handle == nullptr)
        return -1;

    _method = HTTP_PROPFIND;
    return _perform();
}

int mgHttpClient::DELETE()
{
    Debug_println("mgHttpClient::DELETE");
    if (_handle == nullptr)
        return -1;

    _method = HTTP_DELETE;
    return _perform();
}

int mgHttpClient::MKCOL()
{
    Debug_println("mgHttpClient::MKCOL");
    if (_handle == nullptr)
        return -1;

    _method = HTTP_MKCOL;
    return _perform();
}

int mgHttpClient::COPY(const char *destination, bool overwrite, bool move)
{
    Debug_println("mgHttpClient::COPY");
    if (_handle == nullptr || destination == nullptr)
        return -1;

    _method = HTTP_MOVE;
    return _perform();
}

int mgHttpClient::MOVE(const char *destination, bool overwrite)
{
    Debug_println("mgHttpClient::MOVE");
    return COPY(destination, overwrite, true);
}

/*
 Execute an HTTP POST against current URL. Returns HTTP result code
 By default, <Content-Type> is set to <application/x-www-form-urlencoded>
 and data pointed to by post_data should be, also.  This can be overriden by
 setting the appropriate content type using set_header().
 <Content-Length> is set based on post_datalen.
*/
int mgHttpClient::POST(const char *post_data, int post_datalen)
{
    Debug_println("mgHttpClient::POST");
    if (_handle == nullptr || post_data == nullptr || post_datalen < 1)
        return -1;

    _method = HTTP_POST;
    _post_data = post_data;
    _post_datalen = post_datalen;

    return _perform();
}

// Execute an HTTP GET against current URL.  Returns HTTP result code
int mgHttpClient::GET()
{
    Debug_println("mgHttpClient::GET");
    if (_handle == nullptr)
        return -1;

    _method = HTTP_GET;
    return _perform();
}

int mgHttpClient::HEAD()
{
    _method = HTTP_HEAD;
    return _perform();
}

// Sets the URL for the next HTTP request
// Existing connection will be closed if this is a different host
bool mgHttpClient::set_url(const char *url)
{
    if (_handle == nullptr)
        return false;

    return true;
}

// Sets an HTTP request header
bool mgHttpClient::set_header(const char *header_key, const char *header_value)
{
    if (_handle == nullptr || header_key == nullptr || header_value == nullptr)
        return false;

    if (_request_headers.size() >= 20)
        return false;

    _request_headers[header_key] = header_value;
    return true;
}

// Returns number of response headers available to read
int mgHttpClient::get_header_count()
{
    return _stored_headers.size();
}

char *mgHttpClient::get_header(int index, char *buffer, int buffer_len)
{
    if (index < 0 || index > (_stored_headers.size() - 1))
        return nullptr;

    if (buffer == nullptr)
        return nullptr;

    auto vi = _stored_headers.begin();
    std::advance(vi, index);
    return strncpy(buffer, vi->second.c_str(), buffer_len);
}

const std::string mgHttpClient::get_header(int index)
{
    if (index < 0 || index > (_stored_headers.size() - 1))
        return std::string();

    auto vi = _stored_headers.begin();
    std::advance(vi, index);
    return vi->second;
}

// Returns value of requested response header or nullptr if there is no match
const std::string mgHttpClient::get_header(const char *header)
{
    std::string hkey(header);
    header_map_t::iterator it = _stored_headers.find(hkey);
    if (it != _stored_headers.end())
        return it->second;
    return std::string();
}

// Specifies names of response headers to be stored from the server response
void mgHttpClient::collect_headers(const char *headerKeys[], const size_t headerKeysCount)
{
    if (_handle == nullptr || headerKeys == nullptr)
        return;

    // Clear out the current headers
    _stored_headers.clear();

    for (int i = 0; i < headerKeysCount; i++)
        _stored_headers.insert(header_entry_t(headerKeys[i], std::string()));
}

#endif // !ESP_PLATFORM