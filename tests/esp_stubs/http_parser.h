// Minimal http_parser.h for host-side unit tests: the http_method names the
// WebDAV method list uses. Values are arbitrary.
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

#endif
