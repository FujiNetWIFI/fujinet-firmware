// Stand-in for ESP-IDF's esp_http_server.h: the types and calls the WebDAV
// server uses. fake_httpd.cpp implements them over an in-memory exchange.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/types.h>
#include <http_parser.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_HTTPD_BASE 0xb000
#define ESP_ERR_HTTPD_INVALID_REQ (ESP_ERR_HTTPD_BASE + 3)
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_TYPE_OCTET "application/octet-stream"
#define HTTPD_MAX_URI_LEN 512

typedef void *httpd_handle_t;

struct httpd_req_t
{
    httpd_handle_t handle;
    int method;
    char uri[HTTPD_MAX_URI_LEN + 1];
    size_t content_len;
    void *aux;
    void *user_ctx;
};

struct httpd_uri_t
{
    const char *uri;
    http_method method;
    esp_err_t (*handler)(httpd_req_t *r);
    void *user_ctx;
    bool is_websocket;
    bool handle_ws_control_frames;
    const char *supported_subprotocol;
};

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler);
size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val,
                                      size_t val_size);
int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len);
esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status);
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value);
esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t buf_len);
esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str);
int httpd_send(httpd_req_t *r, const char *buf, size_t buf_len);
