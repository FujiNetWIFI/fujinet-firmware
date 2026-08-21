/* Shared REST API (/api/v1) logic for the web UI.

   The same API is served by two different web servers: esp_http_server on the
   ESP32 (httpService.cpp) and mongoose on FujiNet-PC (mgHttpService.cpp).
   Everything below is server-agnostic - a request comes in as a path, a method
   and an optional body, and a fully formed JSON response comes back - so
   neither server's types leak in here and each server keeps only a thin
   adapter that reads the body, calls handle(), and sends the result.

   Routing lives here too, so the endpoint list cannot drift between the two
   servers: each one registers a single catch-all route under FN_API_ROOT and
   hands everything below it to handle().
*/

#ifndef HTTPSERVICEAPI_H
#define HTTPSERVICEAPI_H

#include <cstddef>
#include <string>

// Path prefix every endpoint lives under. Used by both servers to register
// their catch-all route, so it has to be usable in a string literal paste.
#define FN_API_ROOT "/api/v1"

namespace fnHttpApi
{
    // Only the methods the API uses; anything else is OTHER and gets a 405.
    enum class method
    {
        GET,
        POST,
        OTHER
    };

    struct response
    {
        std::string body; // JSON; always populated, errors included
        int status = 200;
    };

    /* Route and run one API request.

       uri is the request path; a query string, if the server left one on, is
       ignored. body may be null for requests that carry none - endpoints that
       need one answer 400 in that case. Always returns something to send: 404
       for an unknown path, 405 when the path is known but the method is not.
    */
    response handle(const std::string &uri, method m, const char *body, size_t body_len);
}

#endif // HTTPSERVICEAPI_H
