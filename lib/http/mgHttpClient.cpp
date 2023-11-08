#ifndef ESP_PLATFORM

// TODO: Figure out why time-outs against bad addresses seem to take about 18s no matter
// what we set the timeout value to.

// workaround to avoid compile error, TODO fix include order
#include "mongoose.h"
#include <cstdlib>
#include <string.h>
#include <map>

#include "../../include/debug.h"
#include "mgHttpClient.h"
#include "fnSystem.h"
#include "utils.h"


#define HTTPCLIENT_WAIT_FOR_CONSUMER_TASK 20000 // 20s
#define HTTPCLIENT_WAIT_FOR_HTTP_TASK 20000     // 20s

#define DEFAULT_HTTP_BUF_SIZE (512)

const char *webdav_depths[] = {"0", "1", "infinity"};

mgHttpClient::mgHttpClient()
{
    _buffer = nullptr; //(char *)malloc(DEFAULT_HTTP_BUF_SIZE);
}

// Close connection, destroy any resoruces
mgHttpClient::~mgHttpClient()
{
    close();

    if (_handle != nullptr)
        mg_mgr_free(_handle);
        // esp_http_client_cleanup(_handle);

    if (_buffer != nullptr) {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient buffer free\n");
#endif
        free(_buffer);
    }
}

// Start an HTTP client session to the given URL
bool mgHttpClient::begin(std::string url)
{
    Debug_printf("mgHttpClient::begin \"%s\"\n", url.c_str());

    // esp_http_client_config_t cfg;
    // memset(&cfg, 0, sizeof(cfg));
    // cfg.url = url.c_str();
    // cfg.event_handler = _httpevent_handler;
    // cfg.user_data = this;
    // cfg.timeout_ms = 3500; // Timeouts seem to actually be twice this value

    // // Keep track of what the max redirect count is set to (the default is 10)
    // _max_redirects = cfg.max_redirection_count == 0 ? 10 : cfg.max_redirection_count;
    // // Keep track of the auth type set
    // _auth_type = cfg.auth_type;
    _max_redirects = 10;

    // _handle = esp_http_client_init(&cfg);

    _handle = new(struct mg_mgr);
    if (_handle == nullptr)
        return false;

    _url = url;
    mg_mgr_init(_handle);
    return true;
}

int mgHttpClient::available()
{
    if (_handle == nullptr)
        return 0;

    int result = 0;

    // int len = esp_http_client_get_content_length(_handle);
    int len = _buffer_len;
    if (len - _buffer_total_read >= 0)
        result = len - _buffer_total_read;

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
    //Debug_println("::read");
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

        // // Go ahead and return if we got as many bytes as requested
        // if (dest_bufflen == bytes_to_copy)
        //     return bytes_to_copy;

        bytes_copied = bytes_to_copy;
    }

    return bytes_copied;

    // // Nothing left to read - later ESP-IDF versions provide esp_http_client_is_complete_data_received()
    // if (_transaction_done)
    // {
    //     //Debug_println("::read download done");
    //     return bytes_copied;
    // }

    // // Make sure store our current task handle to respond to
    // _taskh_consumer = xTaskGetCurrentTaskHandle();

    // // Our HTTP subtask is gone - say there's nothing left to read...
    // if (_taskh_subtask == nullptr)
    // {
    //     Debug_println("::read subtask gone");
    //     return bytes_copied;
    // }

    // while (bytes_copied < dest_bufflen)
    // {
    //     // Let the HTTP process task know to fill the buffer
    //     //Debug_println("::read notifyGive");
    //     xTaskNotifyGive(_taskh_subtask);
    //     // Wait till the HTTP task lets us know it's filled the buffer
    //     //Debug_println("::read notifyTake...");
    //     if(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_HTTP_TASK)) != 1)
    //     {
    //         // Abort if we timed-out receiving the data
    //         Debug_println("::read time-out");
    //         return -1;
    //     }
    //     //Debug_println("::read got notification");
    //     if (_buffer_len <= 0)
    //     {
    //         //Debug_println("::read download done");
    //         return bytes_copied;
    //     }

    //     int dest_size = dest_bufflen - bytes_copied;
    //     bytes_to_copy = dest_size > _buffer_len ? _buffer_len : dest_size;

    //     //Debug_printf("dest_size=%d, dest_bufflen=%d, bytes_copied=%d, bytes_to_copy=%d\n",
    //                  //dest_size, dest_bufflen, bytes_copied, bytes_to_copy);

    //     memcpy(dest_buffer + bytes_copied, _buffer, bytes_to_copy);
    //     _buffer_pos += bytes_to_copy;
    //     _buffer_total_read += bytes_to_copy;
    //     bytes_copied += bytes_to_copy;
    // }

    // return bytes_copied;
}

// Thorws out any waiting response body without closing the connection
void mgHttpClient::_flush_response()
{
//     //Debug_println("mgHttpClient::flush_response");
//     if (_handle == nullptr)
//         return;

//     _buffer_len = 0;
//     esp_http_client_set_post_field(_handle, nullptr, 0);

//     // Nothing left to read
//     if (_transaction_done)
//         return;

//     // Our HTTP subtask is gone - nothing to do
//     if (_taskh_subtask == nullptr)
//         return;

//     // Make sure store our current task handle to respond to
//     _taskh_consumer = xTaskGetCurrentTaskHandle();
//     do
//     {
//         // Let the HTTP process task know to fill the buffer
//         //Debug_println("::flush_response notifyGive");
//         xTaskNotifyGive(_taskh_subtask);
//         // Wait till the HTTP task lets us know it's filled the buffer
//         //Debug_println("::flush_response notifyTake...");
//         ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_HTTP_TASK));

//     } while (!_transaction_done);
//     //Debug_println("mgHttpClient::flush_response done");
}

// Close connection, but keep request resources
void mgHttpClient::close()
{
    Debug_println("mgHttpClient::close");
//     _delete_subtask_if_running();

//     if (_handle != nullptr)
//         esp_http_client_close(_handle);

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
// esp_err_t mgHttpClient::_httpevent_handler(esp_http_client_event_t *evt)
void mgHttpClient::_httpevent_handler(struct mg_connection *c, int ev, void *ev_data, void *user_data)
{
    // // Our user_data should be a pointer to our mgHttpClient object
    // mgHttpClient *client = (mgHttpClient *)evt->user_data;
    mgHttpClient *client = (mgHttpClient *)user_data;
    bool progress = true;
    
    switch (ev)
    {
    case MG_EV_CONNECT:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Connected\n");
#endif
        // Connected to server.
        // extract host name from URL
        const char *url = client->_url.c_str();
        struct mg_str host = mg_url_host(url);
        // If url is https://, tell client connection to use TLS
        if (mg_url_is_ssl(url))
        {
            struct mg_tls_opts opts = {};
#ifdef SKIP_SERVER_CERT_VERIFY                
            opts.ca = nullptr; // disable certificate checking 
#else
            opts.ca = "data/ca.pem";
#endif
            opts.srvname = host;
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
        // Response received. Print it
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: HTTP response\n");
        Debug_printf("  Status: %.*s\n", (int)hm->uri.len, hm->uri.ptr);
        Debug_printf("  Received: %lu\n", (unsigned long)hm->message.len);
        // Debug_printf("Header0: %.*s = %.*s\n", (int)hm->headers[0].name.len, hm->headers[0].name.ptr, (int)hm->headers[0].value.len, hm->headers[0].value.ptr);
        Debug_printf("  Body: %lu bytes\n", (unsigned long)hm->body.len);
#endif

        // get response status code and content length
        client->_status_code = std::stoi(std::string(hm->uri.ptr, hm->uri.len));
        client->_content_length = (int)hm->body.len;

        if (client->_status_code == 301 || client->_status_code == 302)
        {
            // remember Location on redirect response
            struct mg_str *loc = mg_http_get_header(hm, "Location");
            if (loc != nullptr)
                client->_location = std::string(loc->ptr, loc->len);
        }

        // get response headers client is interested in
        size_t max_headers = sizeof(hm->headers) / sizeof(hm->headers[0]);
        for (int i = 0; i < max_headers && hm->headers[i].name.len > 0; i++) 
        {
            // Check to see if we should store this response header
            if (client->_stored_headers.size() <= 0)
                break;

            struct mg_str *name = &hm->headers[i].name;
            struct mg_str *value = &hm->headers[i].value;
            std::string hkey(std::string(name->ptr, name->len));
            header_map_t::iterator it = client->_stored_headers.find(hkey);
            if (it != client->_stored_headers.end())
            {
                std::string hval(std::string(value->ptr, value->len));
                it->second = hval;
            }
        }

        // allocate buffer for received data
        if (client->_buffer != nullptr) 
        {
            // new buffer needed after redirect
#ifdef VERBOSE_HTTP
            Debug_printf("    buffer realloc(%d)\n", hm->body.len);
#endif
            client->_buffer = (char *)realloc(client->_buffer, hm->body.len);
        }
        else {
#ifdef VERBOSE_HTTP
            Debug_printf("    buffer malloc(%d)\n", hm->body.len);
#endif
            client->_buffer = (char *)malloc(hm->body.len);
        }
        // copy received data into buffer
        client->_buffer_pos = 0;
        if (client->_buffer != nullptr) {
            client->_buffer_len = hm->body.len;
            memcpy(client->_buffer, hm->body.ptr, client->_buffer_len);
        }
        else {
            client->_buffer_len = 0;
            if (hm->body.len != 0) {
                Debug_printf("mgHttpClient buffer not allocated!");
            }
        }

        c->is_closing = 1;          // Tell mongoose to close this connection
        client->_processed = true;  // Tell event loop to stop
        break;
    }

    case MG_EV_HTTP_CHUNK:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: HTTP chunk (partial msg)\n");
#endif
        break;
    }

    case MG_EV_READ:
    {
#ifdef VERBOSE_HTTP
        Debug_printf("mgHttpClient: Data received\n");
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
        Debug_printf("mgHttpClient event: %d\n", ev);
#endif
        break;
    }
    }

    client->_progressed = progress;

    // switch (evt->event_id)
    // {
    // case HTTP_EVENT_ERROR: // This event occurs when there are any errors during execution
    //     //Debug_printf("HTTP_EVENT_ERROR %u\n", uxTaskGetStackHighWaterMark(nullptr));
    //     break;
    // case HTTP_EVENT_ON_CONNECTED: // Once the HTTP has been connected to the server, no data exchange has been performed
    //     //Debug_printf("HTTP_EVENT_ON_CONNECTED %u\n", uxTaskGetStackHighWaterMark(nullptr));
    //     client->connected = true;
    //     break;
    // case HTTP_EVENT_HEADER_SENT: // After sending all the headers to the server
    //     //Debug_printf("HTTP_EVENT_HEADER_SENT %u\n", uxTaskGetStackHighWaterMark(nullptr));
    //     break;

    // case HTTP_EVENT_ON_HEADER: // Occurs when receiving each header sent from the server
    // {
    //     //Debug_printf("HTTP_EVENT_ON_HEADER %u\n", uxTaskGetStackHighWaterMark(nullptr));
    //     // Check to see if we should store this response header
    //     if (client->_stored_headers.size() <= 0)
    //         break;

    //     std::string hkey(evt->header_key);
    //     header_map_t::iterator it = client->_stored_headers.find(hkey);
    //     if (it != client->_stored_headers.end())
    //     {
    //         std::string hval(evt->header_value);
    //         it->second = hval;
    //     }
    //     break;
    // }
    // case HTTP_EVENT_ON_DATA: // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
    // {
    //     //Debug_printf("HTTP_EVENT_ON_DATA %u\n", uxTaskGetStackHighWaterMark(nullptr));

    //     // Don't do any of this if we're told to ignore the response
    //     if (client->_ignore_response_body == true)
    //         break;

    //     // esp_http_client will automatically retry redirects, so ignore all but the last attemp
    //     int status = esp_http_client_get_status_code(client->_handle);
    //     if ((status == HttpStatus_Found || status == HttpStatus_MovedPermanently) && client->_redirect_count < (client->_max_redirects - 1))
    //     {
    //         //Debug_println("Ignoring redirect response");
    //         break;
    //     }
    //     /*
    //      If auth type is set to NONE, esp_http_client will automatically retry auth failures by attempting to set the auth type to
    //      BASIC or DIGEST depending on the server response code. Ignore this attempt.
    //     */
    //     if (status == HttpStatus_Unauthorized && client->_auth_type == HTTP_AUTH_TYPE_NONE && client->_redirect_count == 0)
    //     {
    //         //Debug_println("Ignoring UNAUTHORIZED response");
    //         break;
    //     }

    //     // Check if this is our first time this event has been triggered
    //     if (client->_transaction_begin == true)
    //     {
    //         client->_transaction_begin = false;
    //         client->_transaction_done = false;
    //         // Let the main thread know we're done reading headers and have moved on to the data
    //         xTaskNotifyGive(client->_taskh_consumer);
    //     }

    //     // Wait to be told we can fill the buffer
    //     //Debug_println("Waiting to start reading");
    //     ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

    //     //Debug_printf("HTTP_EVENT_ON_DATA Data: %p, Datalen: %d\n", evt->data, evt->data_len);

    //     client->_buffer_pos = 0;
    //     client->_buffer_len = (evt->data_len > DEFAULT_HTTP_BUF_SIZE) ? DEFAULT_HTTP_BUF_SIZE : evt->data_len;
    //     memcpy(client->_buffer, evt->data, client->_buffer_len);

    //     // Now let the reader know there's data in the buffer
    //     xTaskNotifyGive(client->_taskh_consumer);
    //     break;
    // }

    // case HTTP_EVENT_ON_FINISH: // Occurs when finish a HTTP session
    // {
    //     // This may get called more than once if esp_http_client decides to retry in order to handle a redirect or auth response
    //     //Debug_printf("HTTP_EVENT_ON_FINISH %u\n", uxTaskGetStackHighWaterMark(nullptr));
    //     // Keep track of how many times we "finish" reading a response from the server
    //     client->_redirect_count++;
    //     break;
    // }

    // case HTTP_EVENT_DISCONNECTED: // The connection has been disconnected
    //     client->connected = false;
    //     //Debug_printf("HTTP_EVENT_DISCONNECTED %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
    //     break;
    // }
    // return ESP_OK;
}

// void mgHttpClient::_perform_subtask(void *param)
// {
//     mgHttpClient *parent = (mgHttpClient *)param;

//     // Reset our transaction state markers
//     parent->_transaction_begin = true;
//     parent->_transaction_done = false;
//     parent->_redirect_count = 0;
//     parent->_buffer_len = 0;

//     //Debug_printf("esp_http_client_perform start\n");

//     esp_err_t e = esp_http_client_perform(parent->_handle);
//     __IGNORE_UNUSED_VAR(e);

//     //Debug_printf("esp_http_client_perform returned %d, stack HWM %u\n", e, uxTaskGetStackHighWaterMark(nullptr));

//     // Indicate there's nothing else to read
//     parent->_transaction_done = true;

//     // Don't send notifications if we're ignoring the response body
//     if (false == parent->_ignore_response_body)
//     {
//         /*
//          If _transaction_begin is false, then we handled the HTTP_EVENT_ON_DATA event, and 
//          read() has sent us a notification we need to accept before continuing.
//         */
//         if (false == parent->_transaction_begin)
//             ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

//         /*
//          If we handled the HTTP_EVENT_ON_DATA event, then read() is waiting for a notification.
//          If we didn't handle that event, then _perform() is waiting for a notification.
//          Notify whichever of the two that they can continue.
//         */
//         xTaskNotifyGive(parent->_taskh_consumer);
//     }

//     //Debug_println("_perform_subtask_exiting");
//     TaskHandle_t tmp = parent->_taskh_subtask;
//     parent->_taskh_subtask = nullptr;
//     vTaskDelete(tmp);
// }

// void mgHttpClient::_delete_subtask_if_running()
// {
//     if (_taskh_subtask != nullptr)
//     {
//         vTaskDelete(_taskh_subtask);
//         _taskh_subtask = nullptr;
//     }
// }

/*
 Performs an HTTP transaction using esp_http_client_perform()
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

    // // Handle the that HTTP task will use to notify us
    // _taskh_consumer = xTaskGetCurrentTaskHandle();

    // // Start a new task to perform the http client work
    // _delete_subtask_if_running();
    // xTaskCreate(_perform_subtask, "perform_subtask", 4096, this, 5, &_taskh_subtask);
    // //Debug_printf("%08lx _perform subtask created\n", fnSystem.millis());

    // // Wait until we have headers returned
    // if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_HTTP_TASK)) == 0)
    // {
    //     Debug_printf("Timed-out waiting for headers to load\n");
    //     //_delete_subtask_if_running();
    //     return -1;
    // }
    //Debug_printf("%08lx _perform notified\n", fnSystem.millis());
    //Debug_printf("Notification of headers loaded\n");

    // bool chunked = esp_http_client_is_chunked_response(_handle);
    // int status = esp_http_client_get_status_code(_handle);
    // int length = esp_http_client_get_content_length(_handle);
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

/*
 Performs an HTTP transaction using esp_http_client_open() and subsequent "streaming" functions.
 Although this is more flexible than the esp_http_client_perform() method, there doesn't
 seem to be a way of retrieving response headers from the server this way.  So we only use
 it for write-focused methods: PUT and arbitrary methods specified using send_request()
*/
// int mgHttpClient::_perform_stream(esp_http_client_method_t method, uint8_t *write_data, int write_size)
// {
//     Debug_printf("%08lx _perform_stream\n", fnSystem.millis());

//     if (_handle == nullptr)
//         return -1;

//     /* Headers added by HttpClient
//         <METHOD> <URI> HTTP/1.1
//         Host: <HOST>
//         User-Agent: <UserAgent>
//         Connection: <keep-alive/close>
//         Accept-Encoding: <encoding>
//         Authorization: Basic <authorization>
//     */

//     // Set method
//     esp_err_t e = esp_http_client_set_method(_handle, method);

//     // Add header specifying expected content size
//     if (write_data != nullptr && write_size > 0)
//     {
//         char buff[12];
//         __itoa(write_size, buff, 10);
//         set_header("Content-Length", buff);
//     }

//     Debug_printf("%08lx _perform_write open+write\n", fnSystem.millis());
//     e = esp_http_client_open(_handle, write_size);
//     if (e != ESP_OK)
//     {
//         Debug_printf("_perform_write error %d during open\n", e);
//         return -1;
//     }

//     e = esp_http_client_write(_handle, (char *)write_data, write_size);
//     if (e < 0)
//     {
//         Debug_printf("_perform_write error during write\n");
//         return -1;
//     }

//     e = esp_http_client_fetch_headers(_handle);
//     if (e < 0)
//     {
//         Debug_printf("_perform_write error during fetch headers\n");
//         return -1;
//     }

//     // Collect results
//     bool chunked = esp_http_client_is_chunked_response(_handle);
//     int status = esp_http_client_get_status_code(_handle);
//     int length = esp_http_client_get_content_length(_handle);
//     Debug_printf("status = %d, length = %d, chunked = %d\n", status, length, chunked ? 1 : 0);

//     // Read any returned data
//     int r = esp_http_client_read(_handle, _buffer, DEFAULT_HTTP_BUF_SIZE);
//     if (r > 0)
//     {
//         _buffer_len = r;
//         Debug_printf("_perform_write read %d bytes\n", r);
//     }

//     return status;
// }

int mgHttpClient::PUT(const char *put_data, int put_datalen)
{
    Debug_println("mgHttpClient::PUT");

    if (_handle == nullptr || put_data == nullptr || put_datalen < 1)
        return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_PUT);
    _method = HTTP_PUT;
    // // See if a content-type has been set and set a default one if not
    // // Call this before esp_http_client_set_post_field() otherwise that function will definitely set the content type to form
    // char *value = nullptr;
    // esp_http_client_get_header(_handle, "Content-Type", &value);
    // if (value == nullptr)
    //     esp_http_client_set_header(_handle, "Content-Type", "application/octet-stream");
    // // esp_http_client_set_post_field() sets the content of the body of the transaction
    // esp_http_client_set_post_field(_handle, put_data, put_datalen);
    _post_data = put_data;
    _post_datalen = put_datalen;

    return _perform();
}

int mgHttpClient::PROPFIND(webdav_depth depth, const char *properties_xml)
{
    Debug_println("mgHttpClient::PROPFIND");
    if (_handle == nullptr)
        return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_PROPFIND);
    _method = HTTP_PROPFIND;
    // // Assume any request body will be XML
    // esp_http_client_set_header(_handle, "Content-Type", "text/xml");
    // // Set depth
    // const char *pDepth = webdav_depths[0];
    // if (depth == DEPTH_1)
    //     pDepth = webdav_depths[1];
    // else if (depth == DEPTH_INFINITY)
    //     pDepth = webdav_depths[2];
    // esp_http_client_set_header(_handle, "Depth", pDepth);

    // // esp_http_client_set_post_field() sets the content of the body of the transaction
    // if (properties_xml != nullptr)
    //     esp_http_client_set_post_field(_handle, properties_xml, strlen(properties_xml));

    return _perform();
}

int mgHttpClient::DELETE()
{
    Debug_println("mgHttpClient::DELETE");
    if (_handle == nullptr)
        return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_DELETE);
    _method = HTTP_DELETE;

    return _perform();
}

int mgHttpClient::MKCOL()
{
    Debug_println("mgHttpClient::MKCOL");
    if (_handle == nullptr)
        return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_MKCOL);
    _method = HTTP_MKCOL;

    return _perform();
}

int mgHttpClient::COPY(const char *destination, bool overwrite, bool move)
{
    Debug_println("mgHttpClient::COPY");
    if (_handle == nullptr || destination == nullptr)
        return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, move ? esp_http_client_method_t::HTTP_METHOD_MOVE : esp_http_client_method_t::HTTP_METHOD_COPY);
    _method = HTTP_MOVE;
    // // Set detination
    // esp_http_client_set_header(_handle, "Destination", destination);
    // // Set overwrite
    // esp_http_client_set_header(_handle, "Overwrite", overwrite ? "T" : "F");

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

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_POST);
    // esp_http_client_set_post_field(_handle, post_data, post_datalen);
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

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_GET);
    _method = HTTP_GET;

    return _perform();
}

int mgHttpClient::HEAD()
{
    // Debug_println("mgHttpClient::HEAD");
    // if (_handle == nullptr)
    //     return -1;

    // // Get rid of any pending data
    // _flush_response();

    // // Set method
    // esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_HEAD);
    _method = HTTP_HEAD;

    return _perform();
}

// Sets the URL for the next HTTP request
// Existing connection will be closed if this is a different host
bool mgHttpClient::set_url(const char *url)
{
    if (_handle == nullptr)
        return false;

    // return ESP_OK == esp_http_client_set_url(_handle, url);
    return true;
}

// Sets an HTTP request header
bool mgHttpClient::set_header(const char *header_key, const char *header_value)
{
    if (_handle == nullptr || header_key == nullptr || header_value == nullptr)
        return false;

    if (_request_headers.size() >= 20)
        return false;

    // esp_err_t e = esp_http_client_set_header(_handle, header_key, header_value);
    // if (e != ESP_OK)
    // {
    //     Debug_printf("mgHttpClient::set_header error %d\n", e);
    //     return false;
    // }
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
        return nullptr;

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