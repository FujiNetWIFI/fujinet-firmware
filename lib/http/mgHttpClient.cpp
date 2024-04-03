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

// #include <mbedtls/debug.h>

#include "fnSystem.h"
#include "utils.h"
#include "mgHttpClient.h"

#include "../../include/debug.h"

#include "mongoose.h"
#undef mkdir

#define HTTPCLIENT_WAIT_FOR_CONSUMER_TASK 20000 // 20s
#define HTTPCLIENT_WAIT_FOR_HTTP_TASK 20000     // 20s

#define DEFAULT_HTTP_BUF_SIZE (512)

const char *webdav_depths[] = {"0", "1", "infinity"};

/*
 * Replaces a piece of chunked html (with size and data) with just the data.
 * Works for multiple blocks.
 * Also ignores any CHUNK EXTENSIONS, e.g. D;foo=bar;baz;qux=123\r\n
 * 
 */
size_t mgHttpClient::process_chunked_data_in_place(char* data) {
    char* input_ptr = data; // Pointer to read from the original data
    char* output_ptr = data; // Pointer to write to the start of the data
    size_t total_length = 0; // Keep track of the total length of de-chunked data

    while (1) {
        // Find the end of the chunk size line
        char* end_of_chunk_size_line = strstr(input_ptr, "\r\n");
        if (!end_of_chunk_size_line) break; // No more complete chunks

        // Find the position of the first semicolon in the chunk size line, if present
        char* semicolon_pos = strchr(input_ptr, ';');
        if (semicolon_pos && semicolon_pos < end_of_chunk_size_line) {
            // If there's a semicolon before the end of the chunk size line, it marks the start of extensions
            *semicolon_pos = '\0'; // Temporarily null-terminate at the semicolon to ignore extensions
        } else {
            // No semicolon found, so null-terminate at the end of the chunk size line for parsing
            *end_of_chunk_size_line = '\0';
        }

        // Parse the chunk size, which is a HEX number of bytes
        unsigned int chunk_size = 0;
        sscanf(input_ptr, "%x", &chunk_size);

        // Restore the modified character (semicolon or carriage return)
        if (semicolon_pos && semicolon_pos < end_of_chunk_size_line) {
            *semicolon_pos = ';'; // Restore semicolon if it was replaced
        } else {
            *end_of_chunk_size_line = '\r'; // Restore carriage return if that was replaced
        }

        if (chunk_size == 0) break; // End of data

        // Move input_ptr to the start of the chunk data, skipping over the chunk size line
        input_ptr = end_of_chunk_size_line + 2;

        // Move the chunk data to the output position
        memmove(output_ptr, input_ptr, chunk_size);

        // Update the pointers and total_length
        input_ptr += chunk_size + 2; // Move past this chunk's data and the following \r\n
        output_ptr += chunk_size;    // Move output_ptr to the end of the moved data
        total_length += chunk_size;  // Add this chunk's size to the total length
    }

    // Null-terminate the final output string - optional but safe the final string always smaller than the starting string
    *output_ptr = '\0';

    return total_length;
}


mgHttpClient::mgHttpClient()
{
    _buffer = nullptr;
    // Used for cert debugging:
    // mbedtls_debug_set_threshold(5);
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
    if (current_message != nullptr) {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient current_message free\n");
#endif
        freeHttpMessage(current_message);
        current_message = nullptr;
    }
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient exiting deconstructor\n");
#endif
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

void mgHttpClient::handle_connect(struct mg_connection *c)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: Connected\n");
#endif
    _transaction_done = false;

    const char *url = _url.c_str();
    struct mg_str host = mg_url_host(url);
    // If url is https://, tell client connection to use TLS
    if (mg_url_is_ssl(url))
    {
        // struct mg_str key_data = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
        struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY                
        opts.ca.ptr = nullptr; // disable certificate checking 
#else
        opts.ca = mg_file_read(&mg_fs_posix, "data/ca.pem");

        // this is how to load the files rather than refer to them by name (for BUILT_IN tls)
        // opts.ca = mg_file_read(&mg_fs_posix, "data/ca.pem");
        // opts.cert = mg_file_read(&mg_fs_posix, "tls/cert.pem");
        // opts.key = mg_file_read(&mg_fs_posix, "tls/private-key.pem");
#endif
        opts.name = host;
        mg_tls_init(c, &opts);
    }

    // reset response status code
    _status_code = -1;

    // get authentication from url, if any provided
    if (mg_url_user(url).len != 0)
    {
        struct mg_str u = mg_url_user(url);
        struct mg_str p = mg_url_pass(url);
        _username = std::string(u.ptr, u.len);
        _password = std::string(p.ptr, p.len);
    }

    // Send request
    switch(_method)
    {
        case HTTP_GET:
        {
            mg_printf(c, "GET %s HTTP/1.0\r\n"
                            "Host: %.*s\r\n",
                            mg_url_uri(url), (int)host.len, host.ptr);
            // send auth header
            if (!_username.empty())
                mg_http_bauth(c, _username.c_str(), _password.c_str());
            // send request headers
            for (const auto& rh: _request_headers)
                mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
            mg_printf(c, "\r\n");
            break;
        }
        case HTTP_PUT:
        case HTTP_POST:
        {
            mg_printf(c, "%s %s HTTP/1.0\r\n"
                            "Host: %.*s\r\n",
                            (_method == HTTP_PUT) ? "PUT" : "POST",
                            mg_url_uri(url), (int)host.len, host.ptr);
            // send auth header
            if (!_username.empty())
                mg_http_bauth(c, _username.c_str(), _password.c_str());
            // set Content-Type if not set
            header_map_t::iterator it = _request_headers.find("Content-Type");
            if (it == _request_headers.end())
                set_header("Content-Type", "application/octet-stream");
            // send request headers
            for (const auto& rh: _request_headers)
                mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
#ifdef VERBOSE_HTTP
            Debug_println("Custom headers");
            for (const auto& rh: _request_headers)
                Debug_printf("  %s: %s\n", rh.first.c_str(), rh.second.c_str());
#endif
            mg_printf(c, "Content-Length: %d\r\n", _post_datalen);
            mg_printf(c, "\r\n");
            mg_send(c, _post_data, _post_datalen);
            break;
        }
        case HTTP_DELETE:
        {
            mg_printf(c, "DELETE %s HTTP/1.0\r\n"
                            "Host: %.*s\r\n",
                            mg_url_uri(url), (int)host.len, host.ptr);
            // send auth header
            if (!_username.empty())
                mg_http_bauth(c, _username.c_str(), _password.c_str());
            // send request headers
            for (const auto& rh: _request_headers)
                mg_printf(c, "%s: %s\r\n", rh.first.c_str(), rh.second.c_str());
            mg_printf(c, "\r\n");
            break;

        }
        default:
        {
#ifdef VERBOSE_HTTP
            Debug_printf("mgHttpClient: method %d is not implemented\n", _method);
#endif
        }
    }
}

void mgHttpClient::deepCopyHttpMessage(const struct mg_http_message *src, struct mg_http_message *dest) {
    // First, shallow copy the entire struct to copy over the non-pointer fields
    *dest = *src;

    // Now, deep copy each mg_str field that contains a pointer
    dest->method.ptr = util_strndup(src->method.ptr, src->method.len);
    dest->uri.ptr = util_strndup(src->uri.ptr, src->uri.len);
    dest->query.ptr = util_strndup(src->query.ptr, src->query.len);
    dest->proto.ptr = util_strndup(src->proto.ptr, src->proto.len);
    dest->body.ptr = util_strndup(src->body.ptr, src->body.len);
    dest->head.ptr = util_strndup(src->head.ptr, src->head.len);
    dest->message.ptr = util_strndup(src->message.ptr, src->message.len);

    // Deep copy headers array
    for (int i = 0; i < MG_MAX_HTTP_HEADERS && src->headers[i].name.len > 0; ++i) {
        dest->headers[i].name.ptr = util_strndup(src->headers[i].name.ptr, src->headers[i].name.len);
        dest->headers[i].value.ptr = util_strndup(src->headers[i].value.ptr, src->headers[i].value.len);
    }
}

void mgHttpClient::freeHttpMessage(struct mg_http_message *msg) {
    // Free each allocated string
    free((void*)msg->method.ptr);
    free((void*)msg->uri.ptr);
    free((void*)msg->query.ptr);
    free((void*)msg->proto.ptr);
    free((void*)msg->body.ptr);
    free((void*)msg->head.ptr);
    free((void*)msg->message.ptr);

    // Free headers
    for (int i = 0; i < MG_MAX_HTTP_HEADERS && msg->headers[i].name.len > 0; ++i) {
        free((void*)msg->headers[i].name.ptr);
        free((void*)msg->headers[i].value.ptr);
    }
}

void mgHttpClient::send_data(struct mg_http_message *hm, int status_code)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: send_data\n");
#endif

    // get response status code and content length
    _status_code = status_code;
    _content_length = (int)hm->body.len;

    if (_status_code == 301 || _status_code == 302)
    {
        // remember Location on redirect response
        struct mg_str *loc = mg_http_get_header(hm, "Location");
        if (loc != nullptr)
            _location = std::string(loc->ptr, loc->len);
    }

    // get response headers client is interested in
    size_t max_headers = sizeof(hm->headers) / sizeof(hm->headers[0]);
    for (int i = 0; i < max_headers && hm->headers[i].name.len > 0; i++) 
    {
        // Check to see if we should store this response header
        if (_stored_headers.size() <= 0)
            break;

        set_header_value(&hm->headers[i].name, &hm->headers[i].value);
    }

    // allocate buffer for received data
    // realloc == malloc if first param is NULL
    _buffer = (char *)realloc(_buffer, hm->body.len);

    // copy received data into buffer
    _buffer_pos = 0;
    if (_buffer != nullptr) {
        _buffer_len = hm->body.len;
        memcpy(_buffer, hm->body.ptr, _buffer_len);
    }
    else {
        _buffer_len = 0;
        if (hm->body.len != 0) {
            Debug_printf("mgHttpClient ERROR: buffer was not allocated for received data.");
        }
    }

}

void mgHttpClient::handle_http_msg(struct mg_connection *c, struct mg_http_message *hm)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: handle_http_msg\n");
    Debug_printf("  Status: %.*s\n", (int) hm->uri.len, hm->uri.ptr);
    Debug_printf("  Received: %ld\n", (unsigned long) hm->message.len);
    Debug_printf("  Body: %ld bytes\n", (unsigned long) hm->body.len);
#endif

    int status_code = std::stoi(std::string(hm->uri.ptr, hm->uri.len));
    send_data(hm, status_code);

    c->is_closing = 1;          // Tell mongoose to close this connection as it's completed
    c->recv.len = 0;            // Reset the buffer to 0
    _processed = true;    // Tell event loop to stop

}

void mgHttpClient::handle_read(struct mg_connection *c)
{
#ifdef VERBOSE_HTTP
    Debug_printf("mgHttpClient: handle_read\n");
#endif

    struct mg_http_message hm;
    int n = mg_http_parse((const char *) c->recv.buf, c->recv.len, &hm);
    // Debug_printf("handle_read, n: %d, recv: %s, recv.len: %d, hm.body: >%s<, len: %d\n", n, c->recv.buf, c->recv.len, hm.body.ptr, hm.body.len);
    if (hm.body.len == -1 && n > 0) {
        is_chunked = true;
        // 1st block of chunked data, decode it. we abuse the string data as we'll empty it at the end anyway. sorry const
        hm.body.len = process_chunked_data_in_place((char *) hm.body.ptr);
        // Debug_printf("[1] about to send '%s' [%d]\n", hm.body.ptr, hm.body.len);

        // keep a copy of the http message for subsequent blocks, we will amend its data. we need the headers kept around
        if (current_message != nullptr) {
            freeHttpMessage(current_message);
        }
        current_message = (struct mg_http_message *) malloc(sizeof(struct mg_http_message));
        deepCopyHttpMessage(&hm, current_message);

        int status_code = std::stoi(std::string(current_message->uri.ptr, current_message->uri.len));
        send_data(current_message, status_code);

        // is this correct? we might get a small chunked block that is complete.
        c->is_closing = 0;
        c->recv.len = 0;
        _processed = true;

    }
    else if (is_chunked) {
        // subsequent chunked data without the http header, just the data
        if (c->recv.len > 0) {
            // Turn the recv buffer into a processable chunk by null terminating it. This stops the chunk processing from running into old data in case this isn't the last chunk.
            c->recv.buf[c->recv.len] = '\0';
        }

        size_t new_len = process_chunked_data_in_place((char *) c->recv.buf);

        // Allocate or reallocate memory for current_message->body.ptr to hold the new data
        char* new_body_ptr = (char*)realloc((void*)current_message->body.ptr, new_len);

        // Since realloc might return a different pointer, update current_message->body.ptr
        current_message->body.ptr = new_body_ptr;

        // Copy the processed data into the newly allocated memory
        memcpy((void*)current_message->body.ptr, c->recv.buf, new_len);

        // Update current_message->body.len with the new length
        current_message->body.len = new_len;

        // Debug_printf("[2] about to send '%s' [%d]\n", current_message.body.ptr, current_message.body.len);
        send_data(current_message, 200);

        c->is_closing = c->recv.len == 0 ? 1 : 0;      // there's more data to get yet, so don't close until we have got the end of message, which is when recv.len is 0.
        c->recv.len = 0;
        // reset the buffer_total_read for this new block of data. If it's over the size that is subsequently requested, it'll come out of the buffer
        _buffer_total_read = 0;
        _processed = true;
    }
    else {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: handle_read ignoring this block\n");
#endif
    }

}

void report_unhandled(int ev)
{
#ifdef VERBOSE_HTTP
    switch(ev)
    {
    case MG_EV_TLS_HS:
        Debug_printf("mgHttpClient: TLS Handshake succeeded\n");
        break;

    case MG_EV_HTTP_HDRS:
        Debug_printf("mgHttpClient: HTTP Headers received\n");
        break;

    case MG_EV_OPEN:
        Debug_printf("mgHttpClient: Open received\n");
        break;

    case MG_EV_WRITE:
        Debug_printf("mgHttpClient: Data written\n");
        break;

    case MG_EV_RESOLVE:
        Debug_printf("mgHttpClient: Host name resolved\n");
        break;

    default:
        Debug_printf("mgHttpClient: Unknown code: %d\n", ev);
        break;

    }
#endif
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
        client->handle_connect(c);
        break;

    case MG_EV_HTTP_MSG:
        client->handle_http_msg(c, (struct mg_http_message *) ev_data);
        break;

    case MG_EV_READ:
        client->handle_read(c);
        break;

    case MG_EV_CLOSE:
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Connection closed\n");
#endif
        client->_transaction_done = true;
        client->is_chunked = false;
        break;
    
    case MG_EV_ERROR:
        Debug_printf("mgHttpClient: Error - %s\n", (const char*)ev_data);
        client->_transaction_done = true;
        client->_processed = true;  // Error, tell event loop to stop
        client->_status_code = 901; // Fake HTTP status code to indicate connection error
        break;
    
    case MG_EV_POLL:
        progress = false;
        break;
    
    default:
        report_unhandled(ev);
        break;

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
    // create client connection if this is a new request. If we were in the middle of processing chunks, we don't redo it.
    if (_transaction_done) {
        _perform_connect();
    }

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

    int status = _status_code;
    int length = _content_length;

    Debug_printf("%08lx _perform status = %d, length = %d, chunked = %d\n", (unsigned long)fnSystem.millis(), status, length, is_chunked ? 1 : 0);
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

// Returns value of requested response header or empty string if there is no match
const std::string mgHttpClient::get_header(const char *header)
{
    std::string hkey = util_tolower(header);
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

    for (int i = 0; i < headerKeysCount; i++) {
        std::string lower_key = util_tolower(headerKeys[i]);
        _stored_headers.insert(header_entry_t(lower_key, std::string()));
    }
}

// Sets the header's value in the map if found (case insensitive)
void mgHttpClient::set_header_value(const struct mg_str *name, const struct mg_str *value)
{
    std::string hkey = util_tolower(std::string(name->ptr, name->len));
    header_map_t::iterator it = _stored_headers.find(hkey);
    if (it != _stored_headers.end())
    {
        std::string hval(std::string(value->ptr, value->len));
        it->second = hval;
    }
}

#endif // !ESP_PLATFORM