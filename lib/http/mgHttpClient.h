#ifndef _MG_HTTPCLIENT_H_
#define _MG_HTTPCLIENT_H_

#include <string>
#include <map>
#include <memory>
#include <cstdint>
#include <vector>

#include "mongoose.h"
#undef mkdir

// http timeout in ms
#define HTTP_CLIENT_TIMEOUT 20000 // 20 s
// while debugging, increase timeout
// #define HTTP_CLIENT_TIMEOUT 600000

// using namespace fujinet;

// on Windows/MinGW DELETE is defined already ...
#if defined(_WIN32) && defined(DELETE)
#undef DELETE
#endif

struct MgMgrDeleter {
    void operator()(mg_mgr* ptr) const {
        if (ptr != nullptr) {
            mg_mgr_free(ptr);
            delete ptr;
        }
    }
};

class mgHttpClient
{
private:
    typedef std::map<std::string,std::string> header_map_t;
    typedef std::pair<std::string,std::string> header_entry_t;

    std::string _url;

    char *_buffer; // Will be allocated to hold message received by mongoose

    // char *_dechunk_buffer; // allocated to handle dechunking
    // will the read keep returning the old data? or can we detect and move on? the service won't be repeating, so we need to reset buffer correctly after dechunking
    // uint16_t old_chunk_length = 0;

    int _buffer_pos;
    int _buffer_len;
    int _buffer_total_read;

    // TaskHandle_t _taskh_consumer = nullptr;
    // TaskHandle_t _taskh_subtask = nullptr;
    bool _processed;
    bool _progressed;

    // bool _chunked;

    bool _ignore_response_body = false;
    bool _transaction_begin;
    bool _transaction_done = true;
    int _redirect_count;
    int _max_redirects;
    bool connected = false;
    // esp_http_client_auth_type_t _auth_type;

    uint16_t _port = 80;
    header_map_t _stored_headers;
    header_map_t _request_headers;

    // esp_http_client_handle_t _handle = nullptr;
    std::unique_ptr<mg_mgr, MgMgrDeleter> _handle;

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
    static void _httpevent_handler(struct mg_connection *c, int ev, void *ev_data);

    // void _delete_subtask_if_running();

    void _flush_response();

    int _perform();
    void _perform_connect();
    // int _perform_stream(esp_http_client_method_t method, uint8_t *write_data, int write_size);

    bool is_chunked = false;
    size_t process_chunked_data_in_place(char* data, size_t upper_bound);
    void handle_connect(struct mg_connection *c);
    void handle_http_msg(struct mg_connection *c, struct mg_http_message *hm);
    void handle_read(struct mg_connection *c);
    void send_data(struct mg_http_message *hm, int status_code);

    void deepCopyHttpMessage(const struct mg_http_message *src, struct mg_http_message *dest);
    void freeHttpMessage(struct mg_http_message *msg);
    struct mg_http_message *current_message = nullptr;
    std::string certDataStorage; // Store the processed certificate data

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

    bool is_transaction_done() { return _transaction_done; }
    int available();

    int read(uint8_t *dest_buffer, int dest_bufflen);

    //int write(const uint8_t *src_buffer, int src_bufflen);

    bool set_url(const char *url);

    bool set_header(const char *header_key, const char *header_value);
    
    const std::string get_header(const char *header);
    const std::string get_header(int index);
    char * get_header(int index, char *buffer, int buffer_len);
    int get_header_count();

    void create_empty_stored_headers(const std::vector<std::string>& headerKeys);
    void set_header_value(const struct mg_str *name, const struct mg_str *value);

    const std::map<std::string, std::string>& get_stored_headers() const {
        return _stored_headers;
    }

    //const char * buffer_contents(int *buffer_len);

    // Certificate handling
    void load_system_certs();
    mg_str ca;

#if defined(_WIN32)
    void load_system_certs_windows();
    std::string concatenatedPEM;
#else
    void load_system_certs_unix();
#endif

};

#endif // _MG_HTTPCLIENT_H_
