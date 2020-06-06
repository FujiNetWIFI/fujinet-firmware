#ifndef _FN_HTTPCLIENT_H_
#define _FN_HTTPCLIENT_H_

#include <string>
#include <map>
#include "../fn_esp_http_client/fn_esp_http_client.h"

using namespace fujinet;

class fnHttpClient
{
private:
    typedef std::map<std::string,std::string> header_map_t;
    typedef std::pair<std::string,std::string> header_entry_t;

    char *_buffer; // Will be allocated to DEFAULT_HTTP_BUF_SIZE
    int _buffer_pos = 0;
    int _buffer_len = 0;

    TaskHandle_t _taskh_consumer = nullptr;
    TaskHandle_t _taskh_subtask = nullptr;

    bool _ignore_response_body = false;
    bool _transaction_begin;
    bool _transaction_done;
    int _redirect_count;
    int _max_redirects;
    esp_http_client_auth_type_t _auth_type;

    uint16_t _port = 80;
    header_map_t _stored_headers;

    esp_http_client_handle_t _handle = nullptr;

    static void _perform_subtask(void *param);
    static esp_err_t _httpevent_handler(esp_http_client_event_t *evt);

    void _delete_subtask_if_running();

    int _perform();
    int _perform_stream(esp_http_client_method_t method, uint8_t *write_data, int write_size);

public:

    fnHttpClient();
    ~fnHttpClient();

    bool begin(std::string url);
    void close();

    int GET();
    int HEAD();
    int POST(const char *post_data, int post_datalen);
    int PUT(const char *put_data, int put_datalen);

    int read(uint8_t *dest_buffer, int dest_bufflen);
    int write(const uint8_t *src_buffer, int src_bufflen);

    bool set_url(const char *url);
    bool set_header(const char *header_key, const char *header_value);

    const std::string get_header(const char *header);
    int get_header_count();

    void collect_headers(const char* headerKeys[], const size_t headerKeysCount);

    const char * buffer_contents(int *buffer_len);
};

#endif // _FN_HTTPCLIENT_H_
