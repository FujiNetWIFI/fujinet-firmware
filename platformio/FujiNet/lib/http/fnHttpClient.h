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

    TaskHandle_t _taskh_consumer = nullptr;
    //TaskHandle_t _taskh_http = nullptr;
    TaskHandle_t _taskh_subtask = nullptr;

    bool _data_download_done = true;

    uint16_t _port = 80;
    header_map_t _stored_headers;

    esp_http_client_handle_t _handle = nullptr;

    static void _perform_subtask(void *param);
    static esp_err_t _httpevent_handler(esp_http_client_event_t *evt);

    void _delete_subtask_if_running();

    int _perform();

public:

    fnHttpClient();
    ~fnHttpClient();

    bool begin(std::string url);
    void close();

    int GET();
    int HEAD();
    int POST(const char *post_data, int post_datalen);

    int read(uint8_t *dest_buffer, int dest_bufflen);
    int write(const uint8_t *src_buffer, int src_bufflen);

    bool set_url(const char *url);
    bool set_header(const char *header_key, const char *header_value);

    const std::string get_header(const char *header);
    int get_header_count();

    void collect_headers(const char* headerKeys[], const size_t headerKeysCount);
};

#endif // _FN_HTTPCLIENT_H_
