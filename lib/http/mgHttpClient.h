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

    std::string _buffer_str; // Will be used to hold the received message

    bool _processed = false;
    bool _progressed = false;

    // bool _ignore_response_body = false;
    bool _transaction_begin = false; // true indicates we're waiting for the response headers
    bool _transaction_done = false;  // true indicates that entire response was received or error occurred
    int _redirect_count = 0;
    int _max_redirects = 0;
    bool connected = false;
    // esp_http_client_auth_type_t _auth_type;

    uint16_t _port = 80;
    header_map_t _stored_headers;
    header_map_t _request_headers;

    // esp_http_client_handle_t _handle = nullptr;
    std::unique_ptr<mg_mgr, MgMgrDeleter> _handle;

    // http response status code and content length
    int _status_code = -1;
    int _content_length = 0;

    // chunked transfer encoding
    bool _is_chunked = false;

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
	static const char *method_to_string(HttpMethod method);

    // data to send to server
    const char *_post_data = nullptr;
    int _post_datalen = 0;

    static void _httpevent_handler(struct mg_connection *c, int ev, void *ev_data);

	void _flush_response();

	int _perform();
    void _perform_connect();
	void _perform_fetch();
	bool _perform_redirect();
	// int _perform_stream(esp_http_client_method_t method, uint8_t *write_data, int write_size);

    void handle_connect(struct mg_connection *c);
    void handle_http_msg(struct mg_connection *c, struct mg_http_message *hm);
    void handle_read(struct mg_connection *c);
	void process_response_headers(mg_connection *c, mg_http_message &hm, int hdrs_len);
	void process_body_data(mg_connection *c, char *data, int len);

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

	bool set_post_data(const char *post_data, int post_datalen);

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
