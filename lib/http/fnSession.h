/* FujiNet web UI session management
 *
 * Transport-agnostic utilities for cookie-based session auth: reading
 * request cookies, building response headers, parsing login redirects,
 * and deciding which routes are public. Shared by both ESP32 (httpService)
 * and FujiNet-PC (mgHttpService) web servers.
 */
#ifndef FNSESSION_H
#define FNSESSION_H

#include <string>

#define FN_SESSION_COOKIE "fnsession"
#define FN_SESSION_LOGIN_PATH "/login"

namespace fnSession
{
    // Pull one cookie's value out of a raw "Cookie:" request header value.
    // Returns "" when the header is null or the cookie is absent.
    std::string cookie_value(const char *cookie_header, const char *name);

    // Value for a Set-Cookie response header (no "Set-Cookie:" prefix, no CRLF).
    std::string set_cookie(const std::string &token);
    std::string clear_cookie();

    // "/login?next=<url-encoded uri[?query]>". 'query' may be null or empty.
    std::string login_url(const std::string &uri, const std::string &query);

    // Accept only a same-origin absolute path ("/..."); anything else becomes "/".
    std::string sanitize_next(const std::string &next);

    // True when the caller is an XHR/JSON client that should get 401+JSON
    // instead of a redirect. Any argument may be null.
    bool wants_json(const char *accept, const char *requested_with, const char *content_type);

    // Routes reachable without a session when a device password IS set.
    // 'query' may be null.
    bool is_public(const char *uri, const char *query);

    // Full HTML login page. 'error' is shown when non-empty.
    std::string render_login_page(const std::string &next, const std::string &error);

    // Body for a 401 to an XHR/JSON caller.
    const char *json_unauthorized();
}

#endif // FNSESSION_H
