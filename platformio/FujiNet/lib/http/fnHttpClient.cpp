#include <string.h>
#include <FreeRTOS.h>
#include "../../include/debug.h"

#include "fnHttpClient.h"

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
    cfg.event_handler = _event_handler;
    cfg.user_data = this;

    _handle = esp_http_client_init(&cfg);
    if(_handle == nullptr)
        return false;
    return true;
}

// Execute an HTTP GET against current URL.  Returns HTTP result code
int fnHttpClient::GET()
{
    if(_handle == nullptr)
        return -1;

    esp_err_t e;
    

    e = esp_http_client_open(_handle, 0);
    if(e != 0)
    {
        Debug_printf("fnHttpClient::GET open failed %d\n", e);
        return -1;
    }
    e = esp_http_client_fetch_headers(_handle);
    bool chunked = esp_http_client_is_chunked_response(_handle);
    int status = esp_http_client_get_status_code(_handle);

    Debug_printf("fetch_headers = %d, status = %d, chunked = %d\n", e, status, chunked ? 1 : 0);
    return status;
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
    if(_handle == nullptr || dest_buffer == nullptr)
        return -1;

    // Use our own buffer if there's still data there
    if(_buffer_pos > 0 && _buffer_pos < _buffer_len)
    {
        int bytes_left = _buffer_len - _buffer_pos;
        int bytes_to_copy = dest_bufflen > bytes_left ? bytes_left : dest_bufflen;
        memcpy(dest_buffer, _buffer + _buffer_pos, bytes_to_copy);
        _buffer_pos += bytes_to_copy;
        return bytes_to_copy;
    }

    // Nothing left to read
    if(_data_download_done)
        return 0;

    // Make sure store our current task handle to respond to
    _taskh_consumer = xTaskGetCurrentTaskHandle();    
    // Let the HTTP process task know to fill the buffer
    xTaskNotifyGive(_taskh_http);
    // Wait till the HTTP task lets us know it's filled the buffer
    uint32_t v = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8000));
    // Abort if we timed-out receiving the data
    if(v != 1)
        return -1;

    int bytes_to_copy = dest_bufflen > _buffer_len ? _buffer_len : dest_bufflen;
    memcpy(dest_buffer, _buffer, bytes_to_copy);
    _buffer_pos += bytes_to_copy;

    return bytes_to_copy;
}

// Close connection, but keep request resources
void fnHttpClient::close()
{
    if(_taskh_process != nullptr)
    {
        vTaskDelete(_taskh_process);
        _taskh_process = nullptr;
    }

    if(_handle != nullptr)
        esp_http_client_close(_handle);
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
esp_err_t fnHttpClient::_event_handler(esp_http_client_event_t *evt)
{
    // Our user_data should be a pointer to our fnHttpClient object
    fnHttpClient *client = (fnHttpClient *)evt->user_data;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:           // This event occurs when there are any errors during execution
        break;
    case HTTP_EVENT_ON_CONNECTED:    // Once the HTTP has been connected to the server, no data exchange has been performed
        break;
    case HTTP_EVENT_HEADER_SENT:     // After sending all the headers to the server
        break;
    case HTTP_EVENT_ON_HEADER:       // Occurs when receiving each header sent from the server
    {
        std::string hkey(evt->header_key);
        header_map_t::iterator it = client->_stored_headers.find(hkey);
        if(it != client->_stored_headers.end())
        {
            std::string hval(evt->header_value);
            it->second = hval;
        }
        break;
    }
    case HTTP_EVENT_ON_DATA:         // Occurs when receiving data from the server, possibly multiple portions of the packet
    {
        // Store our task handle so we can be notified when to continue
        client->_taskh_http = xTaskGetCurrentTaskHandle();

        // Assume a value of -1 for _buffer_len means this is our first time through this loop
        if(client->_buffer_len == -1)
        {
            // Let the main thread know we're done reading headers and have moved on to the data
            client->_data_download_done = false;
            xTaskNotifyGive(client->_taskh_consumer);
        }
        // Wait to be told we can fill the buffer
        Debug_println("Waiting to start reading");
        ulTaskNotifyTake(1, portMAX_DELAY);

        Debug_printf("HTTP_EVENT_ON_DATA Data: %p, Datalen: %d\n", evt->data, evt->data_len);

        client->_buffer_pos = 0;
        client->_buffer_len = (evt->data_len > DEFAULT_HTTP_BUF_SIZE) ? DEFAULT_HTTP_BUF_SIZE : evt->data_len;
        memcpy(client->_buffer, evt->data, client->_buffer_len);

        // Now let the reader know there's data in the buffer
        xTaskNotifyGive(client->_taskh_consumer);
        break;
    }
    case HTTP_EVENT_ON_FINISH:       // Occurs when finish a HTTP session
        client->_data_download_done = true;
        break;
    case HTTP_EVENT_DISCONNECTED:    // The connection has been disconnected
        break;
    default:
        break;
    }
    return ESP_OK;
}

void fnHttpClient::proceed_task(void *param)
{
    esp_http_client_perform(((fnHttpClient *)param)->_handle);
    do {
        vTaskDelay(pdMS_TO_TICKS(2000));
    } while (true);
}

int fnHttpClient::proceed()
{
    // Reset our buffer position
    _buffer_len = _buffer_pos = -1;
    // Handle the that HTTP task will use to notify us
    _taskh_consumer = xTaskGetCurrentTaskHandle();
    // Start a new task to perform the http client work
    if(_taskh_process != nullptr)
        vTaskDelete(_taskh_process);
    xTaskCreate(proceed_task, "fnHttpClientProceed", 4096, this, 5, &_taskh_process);

    // Wait until we have headers returned
    if(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8000)) == 0)
    {
        Debug_printf("Timed-out waiting for headers to load\n");
        vTaskDelete(_taskh_process);
        return -1;
    }
    Debug_printf("Notification of headers loaded\n");

    bool chunked = esp_http_client_is_chunked_response(_handle);
    int status = esp_http_client_get_status_code(_handle);
    int length = esp_http_client_get_content_length(_handle);

    Debug_printf("status = %d, length = %d, chunked = %d\n", status, length, chunked ? 1 : 0);
    return 0;
}

// Returns value of requested header or nullptr if there is no such header
const std::string fnHttpClient::get_header(const char *header)
{
    std::string hkey(header);
    header_map_t::iterator it = _stored_headers.find(hkey);
    if(it != _stored_headers.end())
        return it->second;
    return std::string();
}

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

// Specifies headers to be stored from the server response
void fnHttpClient::collect_headers(const char* headerKeys[], const size_t headerKeysCount)
{
    if(_handle == nullptr || headerKeys == nullptr)
        return;

    // Clear out the current headers
    _stored_headers.clear();

    for (int i = 0; i < headerKeysCount; i++)
        _stored_headers.insert(header_entry_t(headerKeys[i], std::string()));
}
