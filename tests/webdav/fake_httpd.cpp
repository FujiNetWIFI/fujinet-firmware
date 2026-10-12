// Fake esp_http_server: routes registered by the WebDAV code are matched the
// way ESP-IDF matches them, and each request runs against an Exchange.
#include "fake_httpd.h"
#include <http/httpService.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct Route
{
    std::string uri;
    http_method method;
    esp_err_t (*handler)(httpd_req_t *);
    void *ctx;
};
static std::vector<Route> routes;

static std::string lower(std::string s)
{
    for (auto &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

// Same rules as ESP-IDF's httpd_uri_match_wildcard(): a trailing '*' matches
// any rest, and '?' before it makes the preceding character optional.
static bool uri_match(const std::string &tpl, const std::string &uri)
{
    size_t tpl_len = tpl.size();
    bool asterisk = tpl_len && tpl[tpl_len - 1] == '*';
    if (asterisk)
        tpl_len--;
    bool quest = tpl_len && tpl[tpl_len - 1] == '?';
    if (quest)
        tpl_len--;
    std::string base = tpl.substr(0, tpl_len);
    std::string path = uri.substr(0, uri.find('?'));
    if (!asterisk && !quest)
        return path == base;
    if (quest && path.size() + 1 == tpl_len && path == base.substr(0, tpl_len - 1))
        return true;
    if (path.compare(0, tpl_len, base) != 0 || path.size() < tpl_len)
        return false;
    return asterisk || path.size() == tpl_len;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *u)
{
    for (auto &r : routes)
        if (r.uri == u->uri && r.method == u->method)
            return ESP_FAIL;
    routes.push_back({u->uri, u->method, u->handler, u->user_ctx});
    return ESP_OK;
}

static Exchange &ex(httpd_req_t *r) { return *(Exchange *)r->aux; }

size_t httpd_req_get_hdr_value_len(httpd_req_t *r, const char *field)
{
    auto it = ex(r).req_headers.find(lower(field));
    return it == ex(r).req_headers.end() ? 0 : it->second.size();
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val,
                                      size_t val_size)
{
    auto it = ex(r).req_headers.find(lower(field));
    if (it == ex(r).req_headers.end() || val_size == 0)
        return ESP_FAIL;
    snprintf(val, val_size, "%s", it->second.c_str());
    return it->second.size() < val_size ? ESP_OK : ESP_FAIL;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)
{
    Exchange &e = ex(r);
    size_t n = std::min(buf_len, e.req_body.size() - e.req_pos);
    memcpy(buf, e.req_body.data() + e.req_pos, n);
    e.req_pos += n;
    return (int)n;
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{
    if (status)
        ex(r).status = status;
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    ex(r).resp_headers["content-type"] = type;
    return ESP_OK;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value)
{
    ex(r).resp_headers[lower(field)] = value;
    return ESP_OK;
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t len)
{
    if (buf && len == HTTPD_RESP_USE_STRLEN)
        len = (ssize_t)strlen(buf);
    if (buf && len > 0)
        ex(r).resp_body.append(buf, (size_t)len);
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, ssize_t len)
{
    return httpd_resp_send(r, buf, len);
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str)
{
    return httpd_resp_send(r, str, HTTPD_RESP_USE_STRLEN);
}

int httpd_send(httpd_req_t *, const char *, size_t len) { return (int)len; }

void fnHttpService::return_http_error(httpd_req_t *req, _fnwserr err)
{
    httpd_resp_set_status(req, (std::to_string((int)err) + " (error page)").c_str());
    httpd_resp_sendstr(req, "error page");
}

int request(http_method method, const std::string &uri, Exchange &e)
{
    bool uri_known = false;
    for (auto &r : routes)
    {
        if (!uri_match(r.uri, uri))
            continue;
        uri_known = true;
        if (r.method != method)
            continue;
        httpd_req_t req{};
        req.method = method;
        snprintf(req.uri, sizeof(req.uri), "%s", uri.c_str());
        req.content_len = e.req_body.size();
        req.aux = &e;
        req.user_ctx = r.ctx;
        if (r.handler(&req) != ESP_OK)
            return -1;
        return atoi(e.status.c_str());
    }
    e.status = uri_known ? "405 Method Not Allowed" : "404 Not Found";
    return atoi(e.status.c_str());
}
