// Minimal http_parser.h for host-side unit tests: the http_method names the
// WebDAV code uses. Values are arbitrary.
#ifndef _TEST_STUB_HTTP_PARSER_H_
#define _TEST_STUB_HTTP_PARSER_H_

enum http_method
{
    HTTP_DELETE,
    HTTP_GET,
    HTTP_HEAD,
    HTTP_POST,
    HTTP_PUT,
    HTTP_OPTIONS,
    HTTP_COPY,
    HTTP_LOCK,
    HTTP_MKCOL,
    HTTP_MOVE,
    HTTP_PROPFIND,
    HTTP_PROPPATCH,
    HTTP_UNLOCK,
};

inline const char *http_method_str(enum http_method m)
{
    switch (m)
    {
    case HTTP_DELETE: return "DELETE";
    case HTTP_GET: return "GET";
    case HTTP_HEAD: return "HEAD";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_OPTIONS: return "OPTIONS";
    case HTTP_COPY: return "COPY";
    case HTTP_LOCK: return "LOCK";
    case HTTP_MKCOL: return "MKCOL";
    case HTTP_MOVE: return "MOVE";
    case HTTP_PROPFIND: return "PROPFIND";
    case HTTP_PROPPATCH: return "PROPPATCH";
    case HTTP_UNLOCK: return "UNLOCK";
    }
    return "<unknown>";
}

#endif
