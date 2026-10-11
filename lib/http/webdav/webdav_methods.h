#pragma once

#include <http_parser.h>

// esp_http_server routes by method, so a method left out here answers 405
// even though webdav_handler() implements it.
inline constexpr http_method WEBDAV_METHODS[] = {
    HTTP_COPY,
    HTTP_DELETE,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_LOCK,
    HTTP_MKCOL,
    HTTP_MOVE,
    HTTP_OPTIONS,
    HTTP_PROPFIND,
    HTTP_PROPPATCH,
    HTTP_PUT,
    HTTP_UNLOCK,
};
