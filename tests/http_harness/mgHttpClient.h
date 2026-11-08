#ifndef HTTP_HARNESS_MGHTTPCLIENT_H
#define HTTP_HARNESS_MGHTTPCLIENT_H

// Test stand-in for lib/http/mgHttpClient.h: records each request in fake_http.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// on Windows/MinGW DELETE is defined already, as in the real header
#if defined(_WIN32) && defined(DELETE)
#undef DELETE
#endif

struct FakeHttpRequest
{
    std::string method;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct FakeHttpServer
{
    std::vector<FakeHttpRequest> requests;
    int status = 200;
    std::string response;
};

extern FakeHttpServer fake_http;

class mgHttpClient
{
public:
    enum webdav_depth
    {
        DEPTH_0 = 0,
        DEPTH_1,
        DEPTH_INFINITY
    };

    bool begin(std::string) { return true; }
    void close() {}

    int GET() { return send("GET", nullptr, 0); }
    int HEAD() { return send("HEAD", nullptr, 0); }
    int POST(const char *data, int len) { return send("POST", data, len); }
    int PUT(const char *data, int len) { return send("PUT", data, len); }
    int PROPFIND(webdav_depth, const char *) { return send("PROPFIND", nullptr, 0); }
    int DELETE() { return send("DELETE", nullptr, 0); }
    int MKCOL() { return send("MKCOL", nullptr, 0); }
    int MOVE(const char *, bool) { return send("MOVE", nullptr, 0); }

    bool is_transaction_done() { return _done; }
    int content_length() const { return _done ? (int)fake_http.response.size() : 0; }
    int available() { return _done ? (int)(fake_http.response.size() - _pos) : 0; }
    void set_keep_alive(bool) {}

    int read(uint8_t *dest, int len)
    {
        int count = std::min(len, available());
        memcpy(dest, fake_http.response.data() + _pos, count);
        _pos += count;
        return count;
    }

    bool set_header(const char *key, const char *value)
    {
        _headers[key] = value;
        return true;
    }

    int get_header_count() { return 0; }
    void create_empty_stored_headers(const std::vector<std::string> &) {}
    const std::map<std::string, std::string> &get_stored_headers() const { return _stored; }

private:
    int send(const char *method, const char *data, int len)
    {
        fake_http.requests.push_back({method, data ? std::string(data, len) : "", _headers});
        _done = true;
        _pos = 0;
        return fake_http.status;
    }

    std::map<std::string, std::string> _headers;
    std::map<std::string, std::string> _stored;
    bool _done = false;
    size_t _pos = 0;
};

#endif // HTTP_HARNESS_MGHTTPCLIENT_H
