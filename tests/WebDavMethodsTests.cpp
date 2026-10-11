// lib/http/webdav: every method webdav_handler() serves must be registered.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "webdav_methods.h"

#include <algorithm>
#include <iterator>

static bool registered(http_method method)
{
    return std::find(std::begin(WEBDAV_METHODS), std::end(WEBDAV_METHODS), method) !=
           std::end(WEBDAV_METHODS);
}

TEST_CASE("downloads and uploads are registered (#1839)")
{
    CHECK(registered(HTTP_GET));
    CHECK(registered(HTTP_PUT));
}

TEST_CASE("every method webdav_handler() dispatches is registered")
{
    for (http_method method : {HTTP_COPY, HTTP_DELETE, HTTP_GET, HTTP_HEAD, HTTP_LOCK,
                               HTTP_MKCOL, HTTP_MOVE, HTTP_OPTIONS, HTTP_PROPFIND,
                               HTTP_PROPPATCH, HTTP_PUT, HTTP_UNLOCK})
        CHECK(registered(method));
    CHECK(std::size(WEBDAV_METHODS) == 12);
}
