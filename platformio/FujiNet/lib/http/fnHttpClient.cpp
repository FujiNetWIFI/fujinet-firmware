#include <string.h>
#include <FreeRTOS.h>
#include "../../include/debug.h"
#include "fnSystem.h"
#include "fnHttpClient.h"

#define HTTPCLIENT_WAIT_FOR_CONSUMER_TASK 30000 // 30s
#define HTTPCLIENT_WAIT_FOR_HTTP_TASK 8000 // 8s

fnHttpClient::fnHttpClient()
{
    _buffer = (char *)malloc(DEFAULT_HTTP_BUF_SIZE);

}

// Close connection, destroy any resoruces
fnHttpClient::~fnHttpClient()
{
    close();

    if(_handle != nullptr)
        esp_http_client_cleanup(_handle);

    free(_buffer);
}

// Start an HTTP client session to the given URL
bool fnHttpClient::begin(std::string url)
{
    esp_http_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.url = url.c_str();
    cfg.event_handler = _httpevent_handler;
    cfg.user_data = this;

    _handle = esp_http_client_init(&cfg);
    if(_handle == nullptr)
        return false;
    return true;
}


int fnHttpClient::write(const uint8_t *src_buffer, int src_bufflen)
{
    return 0;
}

/*
 Reads HTTP response data
 Return value is bytes stored in buffer or -1 on error
 Buffer will NOT be zero-terminated
 Bytes copied may be less than buffer size even when there's more data to read
 Return value of zero indicates end of data
*/
int fnHttpClient::read(uint8_t *dest_buffer, int dest_bufflen)
{
    Debug_println("::read");
    if(_handle == nullptr || dest_buffer == nullptr)
        return -1;

    // Use our own buffer if there's still data there
    if(_buffer_pos > 0 && _buffer_pos < _buffer_len)
    {
        int bytes_left = _buffer_len - _buffer_pos;
        int bytes_to_copy = dest_bufflen > bytes_left ? bytes_left : dest_bufflen;
        Debug_printf("::read from buffer %d\n", bytes_to_copy);
        memcpy(dest_buffer, _buffer + _buffer_pos, bytes_to_copy);
        _buffer_pos += bytes_to_copy;
        return bytes_to_copy;
    }

    // Nothing left to read - later ESP-IDF versions provide esp_http_client_is_complete_data_received()
    if(_data_download_done)
    {
        Debug_println("::read download done");
        return 0;
    }

    // Make sure store our current task handle to respond to
    _taskh_consumer = xTaskGetCurrentTaskHandle();

    // Our HTTP subtask is gone - say there's nothing left to read...
    if(_taskh_subtask == nullptr)
    {
        Debug_println("::read subtask gone");
        return 0;
    }

    // Let the HTTP process task know to fill the buffer
    Debug_println("::read notifyGive");
    xTaskNotifyGive(_taskh_subtask);
    // Wait till the HTTP task lets us know it's filled the buffer
    Debug_println("::read notifyTake...");
    uint32_t v = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_HTTP_TASK));
    // Abort if we timed-out receiving the data
    if(v != 1)
    {
        Debug_println("::read time-out");
        return -1;
    }
    Debug_println("::read got notification");
    if(_data_download_done || _buffer_len < 0)
    {
        Debug_println("::read download done");
        return 0;
    }

    int bytes_to_copy = dest_bufflen > _buffer_len ? _buffer_len : dest_bufflen;
    memcpy(dest_buffer, _buffer, bytes_to_copy);
    _buffer_pos += bytes_to_copy;

    return bytes_to_copy;
}

// Close connection, but keep request resources
void fnHttpClient::close()
{
    Debug_println("::close");
    _delete_subtask_if_running();
    Debug_println("::close deleted subtask");
    if(_handle != nullptr)
        esp_http_client_close(_handle);
    Debug_println("::close closed client");
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
esp_err_t fnHttpClient::_httpevent_handler(esp_http_client_event_t *evt)
{
    // Our user_data should be a pointer to our fnHttpClient object
    fnHttpClient *client = (fnHttpClient *)evt->user_data;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:           // This event occurs when there are any errors during execution
        Debug_printf("HTTP_EVENT_ERROR %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        break;
    case HTTP_EVENT_ON_CONNECTED:    // Once the HTTP has been connected to the server, no data exchange has been performed
        Debug_printf("HTTP_EVENT_ON_CONNECTED %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        break;
    case HTTP_EVENT_HEADER_SENT:     // After sending all the headers to the server
        Debug_printf("HTTP_EVENT_HEADER_SENT %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        break;
    case HTTP_EVENT_ON_HEADER:       // Occurs when receiving each header sent from the server
    {
        Debug_printf("HTTP_EVENT_ON_HEADER %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        std::string hkey(evt->header_key);
        header_map_t::iterator it = client->_stored_headers.find(hkey);
        if(it != client->_stored_headers.end())
        {
            std::string hval(evt->header_value);
            it->second = hval;
        }
        break;
    }
    case HTTP_EVENT_ON_DATA:         // Occurs multiple times when receiving body data from the server. MAY BE SKIPPED IF BODY IS EMPTY!
    {
        Debug_printf("HTTP_EVENT_ON_DATA %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        // Assume a value of -1 for _buffer_len means this is our first time through this loop
        if(client->_buffer_len == -1)
        {
            // Let the main thread know we're done reading headers and have moved on to the data
            client->_data_download_done = false;
            xTaskNotifyGive(client->_taskh_consumer);
        }
        // Wait to be told we can fill the buffer
        Debug_println("Waiting to start reading");
        ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

        Debug_printf("HTTP_EVENT_ON_DATA Data: %p, Datalen: %d\n", evt->data, evt->data_len);

        client->_buffer_pos = 0;
        client->_buffer_len = (evt->data_len > DEFAULT_HTTP_BUF_SIZE) ? DEFAULT_HTTP_BUF_SIZE : evt->data_len;
        memcpy(client->_buffer, evt->data, client->_buffer_len);

        // Now let the reader know there's data in the buffer
        xTaskNotifyGive(client->_taskh_consumer);
        break;
    }
    case HTTP_EVENT_ON_FINISH:       // Occurs when finish a HTTP session
    {
        Debug_printf("HTTP_EVENT_ON_FINISH %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        // We're going to wait here to be called for a final READ (returning 0)
        // If _buff_len is still -1 we skipped the DATA event, so don't wait to be notified: the original _perform() function is still waiting for us to tell it we're done
        if(client->_buffer_len != -1)
            ulTaskNotifyTake(1, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_CONSUMER_TASK));

        // Indicate there's nothing else to read
        client->_data_download_done = true;
        client->_buffer_pos = -1;
        client->_buffer_len = -1;

        // Tell either the _perform() or read() tasks we're done
        xTaskNotifyGive(client->_taskh_consumer);
        break;
    }
    case HTTP_EVENT_DISCONNECTED:    // The connection has been disconnected
        Debug_printf("HTTP_EVENT_DISCONNECTED %p:\"%s\":%u\n", xTaskGetCurrentTaskHandle(), pcTaskGetTaskName(nullptr), uxTaskGetStackHighWaterMark(nullptr));
        break;
    default:
        break;
    }
    return ESP_OK;
}

void fnHttpClient::_perform_subtask(void *param)
{
    fnHttpClient *parent = (fnHttpClient *)param;
    esp_http_client_perform(parent->_handle);

    Debug_println("_perform_subtask_exiting");
    TaskHandle_t tmp = parent->_taskh_subtask;
    parent->_taskh_subtask = nullptr;
    vTaskDelete(tmp);
    Debug_println("_perform_subtask LIFE AFTER DEATH");
}

void fnHttpClient::_delete_subtask_if_running()
{
    Debug_println("_delete_subtask_if_running");
    if(_taskh_subtask != nullptr)
    {
        vTaskDelete(_taskh_subtask);
        _taskh_subtask = nullptr;
    }
}

int fnHttpClient::_perform()
{
    Debug_printf("%08lx _perform\n", fnSystem.millis());
    // Reset our buffer position
    _buffer_len = _buffer_pos = -1;
    // Handle the that HTTP task will use to notify us
    _taskh_consumer = xTaskGetCurrentTaskHandle();
    // Start a new task to perform the http client work
    _delete_subtask_if_running();
    xTaskCreate(_perform_subtask, "perform_subtask", 4096, this, 5, &_taskh_subtask);
    Debug_printf("%08lx _perform subtask created\n", fnSystem.millis());

    // Wait until we have headers returned
    if(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTPCLIENT_WAIT_FOR_HTTP_TASK)) == 0)
    {
        Debug_printf("Timed-out waiting for headers to load\n");
        _delete_subtask_if_running();
        Debug_println("Deleted subtask, returning");
        return -1;
    }
    Debug_printf("%08lx _perform notified\n", fnSystem.millis());
    //Debug_printf("Notification of headers loaded\n");

    bool chunked = esp_http_client_is_chunked_response(_handle);
    int status = esp_http_client_get_status_code(_handle);
    int length = esp_http_client_get_content_length(_handle);

    Debug_printf("status = %d, length = %d, chunked = %d\n", status, length, chunked ? 1 : 0);
    return status;
}

/*
 Execute an HTTP POST against current URL. Returns HTTP result code
 By default, <Content-Type> is set to <application/x-www-form-urlencoded>
 and data pointed to by post_data should be, also.  This can be overriden by
 setting the appropriate content type using set_header().
 <Content-Length> is set based on post_datalen.
*/
int fnHttpClient::POST(const char * post_data, int post_datalen)
{
    if(_handle == nullptr || post_data == nullptr || post_datalen < 1)
        return -1;

    // Set method
    esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_POST);
    esp_http_client_set_post_field(_handle, post_data, post_datalen);

    return _perform();
}

// Execute an HTTP GET against current URL.  Returns HTTP result code
int fnHttpClient::GET()
{
    if(_handle == nullptr)
        return -1;

    // Set method
    esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_GET);

    return _perform();
}

int fnHttpClient::HEAD()
{
    if(_handle == nullptr)
        return -1;

    // Set method
    esp_http_client_set_method(_handle, esp_http_client_method_t::HTTP_METHOD_HEAD);

    return _perform();
}

// Sets the URL for the next HTTP request
// Existing connection will be closed if this is a different host
bool fnHttpClient::set_url(const char *url)
{
    if(_handle == nullptr)
        return false;
    
    return ESP_OK == esp_http_client_set_url(_handle, url);
}

// Sets an HTTP request header
bool fnHttpClient::set_header(const char *header_key, const char *header_value)
{
    if(_handle == nullptr)
        return false;

    esp_err_t e = esp_http_client_set_header(_handle, header_key, header_value);
    if(e != ESP_OK)
    {
        Debug_printf("fnHttpClient::set_header error %d\n", e);
        return false;
    }
    return true;
}

// Returns number of response headers available to read
int fnHttpClient::get_header_count()
{
    return _stored_headers.size();
}

// Returns value of requested response header or nullptr if there is no match
const std::string fnHttpClient::get_header(const char *header)
{
    std::string hkey(header);
    header_map_t::iterator it = _stored_headers.find(hkey);
    if(it != _stored_headers.end())
        return it->second;
    return std::string();
}

// Specifies names of response headers to be stored from the server response
void fnHttpClient::collect_headers(const char* headerKeys[], const size_t headerKeysCount)
{
    if(_handle == nullptr || headerKeys == nullptr)
        return;

    // Clear out the current headers
    _stored_headers.clear();

    for (int i = 0; i < headerKeysCount; i++)
        _stored_headers.insert(header_entry_t(headerKeys[i], std::string()));
}
