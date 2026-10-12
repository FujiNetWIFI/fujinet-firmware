// Stand-in for lib/http/httpService.h: only what the WebDAV handler calls.
#pragma once
#include <esp_http_server.h>

class fnHttpService
{
public:
    enum _fnwserr : int {};
    static void return_http_error(httpd_req_t *req, _fnwserr err);
};
