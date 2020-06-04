#ifndef _FN_HTTPCLIENT_H_
#define _FN_HTTPCLIENT_H_

#include <string>
#include <map>
#include <esp_http_client.h>


class fnHttpClient
{
private:
    typedef std::map<std::string,std::string> header_map_t;
    typedef std::pair<std::string,std::string> header_entry_t;

    char *_buffer; // Will be allocated to DEFAULT_HTTP_BUF_SIZE
    int _buffer_pos = 0;
    int _buffer_len = 0;

    //SemaphoreHandle_t _finished_reading_headers = nullptr;
    //SemaphoreHandle_t _read_data = nullptr;

    TaskHandle_t _taskh_consumer = nullptr;
    TaskHandle_t _taskh_http = nullptr;
    TaskHandle_t _taskh_process = nullptr;

    bool _data_download_done = true;

    uint16_t _port = 80;
    header_map_t _stored_headers;

    esp_http_client_handle_t _handle = nullptr;

public:


    fnHttpClient();
    ~fnHttpClient();

    bool begin(std::string url);
    void close();

    int GET();

    int read(uint8_t *dest_buffer, int dest_bufflen);

    bool set_header(const char *header_key, const char *header_value);
    const std::string get_header(const char *header);

    static esp_err_t _event_handler(esp_http_client_event_t *evt);
    int proceed();

    static void proceed_task(void *param);
    void collect_headers(const char* headerKeys[], const size_t headerKeysCount);
};

#endif // _FN_HTTPCLIENT_H_
