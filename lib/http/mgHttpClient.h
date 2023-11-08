#ifndef _MG_HTTPCLIENT_H_
#define _MG_HTTPCLIENT_H_

#include <string>
#include <map>

// http timeout in ms
#define HTTP_TIMEOUT 7000

// using namespace fujinet;

// on Windows/MinGW DELETE is somewhere defined already
#ifdef DELETE
#undef DELETE
#endif

class mgHttpClient
{
private:
    typedef std::map<std::string,std::string> header_map_t;
    typedef std::pair<std::string,std::string> header_entry_t;

    std::string _url;

    char *_buffer; // Will be allocated to hold message received by mongoose
    int _buffer_pos;
    int _buffer_len;
    int _buffer_total_read;

    // TaskHandle_t _taskh_consumer = nullptr;
    // TaskHandle_t _taskh_subtask = nullptr;
    bool _processed;
    bool _progressed;

    bool _ignore_response_body = false;
    bool _transaction_begin;
    bool _transaction_done;
    int _redirect_count;
    int _max_redirects;
    bool connected = false;
    // esp_http_client_auth_type_t _auth_type;

    uint16_t _port = 80;
    header_map_t _stored_headers;
    header_map_t _request_headers;

    // esp_http_client_handle_t _handle = nullptr;
    struct mg_mgr *_handle;

    // http response status code and content length
    int _status_code;
    int _content_length;

    // authentication
    std::string _username;
    std::string _password;

    // http redirect location
    std::string _location;

    // HTTP methods
    enum HttpMethod
    {
        HTTP_GET,
        HTTP_POST,
        HTTP_PUT,
        HTTP_DELETE,
        HTTP_HEAD,
        HTTP_COPY,
        HTTP_MOVE,
        HTTP_MKCOL,
        HTTP_PROPFIND,
    };
    HttpMethod _method;

    // data to send to server
    const char *_post_data;
    int _post_datalen;

    // static void _perform_subtask(void *param);
    // static esp_err_t _httpevent_handler(esp_http_client_event_t *evt);
    static void _httpevent_handler(struct mg_connection *c, int ev, void *ev_data, void *user_data);

    // void _delete_subtask_if_running();

    void _flush_response();

    int _perform();
    void _perform_connect();
    // int _perform_stream(esp_http_client_method_t method, uint8_t *write_data, int write_size);

public:

    mgHttpClient();
    ~mgHttpClient();

    enum webdav_depth
    {
        DEPTH_0 = 0,
        DEPTH_1,
        DEPTH_INFINITY
    };

    bool begin(std::string url);
    void close();

    int GET();
    int HEAD();
    int POST(const char *post_data, int post_datalen);
    int PUT(const char *put_data, int put_datalen);
    int PROPFIND(webdav_depth depth, const char *properties_xml);
    int DELETE();
    int MKCOL();
    int COPY(const char *destination, bool overwrite, bool move = false);
    int MOVE(const char *destination, bool overwrite);

    int available();

    int read(uint8_t *dest_buffer, int dest_bufflen);

    //int write(const uint8_t *src_buffer, int src_bufflen);

    bool set_url(const char *url);

    bool set_header(const char *header_key, const char *header_value);
    
    const std::string get_header(const char *header);
    const std::string get_header(int index);
    char * get_header(int index, char *buffer, int buffer_len);
    int get_header_count();

    void collect_headers(const char* headerKeys[], const size_t headerKeysCount);

    //const char * buffer_contents(int *buffer_len);
};

#endif // _MG_HTTPCLIENT_H_
