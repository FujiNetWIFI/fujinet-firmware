#include "fnSession.h"

#include <cctype>
#include <cstring>
#include <strings.h> // strcasecmp

#include "fileManager.h"
#include "httpServiceParser.h"

namespace fnSession
{

std::string cookie_value(const char *cookie_header, const char *name)
{
    if (cookie_header == nullptr || name == nullptr)
        return "";

    size_t namelen = strlen(name);
    std::string header(cookie_header);

    // Split on ';' and trim spaces
    size_t pos = 0;
    while (pos < header.size())
    {
        // Skip leading spaces
        while (pos < header.size() && header[pos] == ' ')
            pos++;

        size_t start = pos;

        // Find the end of this cookie (semicolon or end of string)
        while (pos < header.size() && header[pos] != ';')
            pos++;

        std::string cookie = header.substr(start, pos - start);

        // Trim trailing spaces from cookie
        while (!cookie.empty() && cookie.back() == ' ')
            cookie.pop_back();

        // Check if this cookie matches
        if (cookie.size() >= namelen + 1 && cookie[namelen] == '=')
        {
            if (strncmp(cookie.c_str(), name, namelen) == 0)
            {
                std::string value = cookie.substr(namelen + 1);
                // Strip surrounding double quotes if present
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                return value;
            }
        }

        // Move past the semicolon
        if (pos < header.size())
            pos++;
    }

    return "";
}

std::string set_cookie(const std::string &token)
{
    return FN_SESSION_COOKIE "=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=31536000";
}

std::string clear_cookie()
{
    return FN_SESSION_COOKIE "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

// URL-encode: percent-encode everything outside A-Za-z0-9-_.~
static std::string url_encode(const std::string &in)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += (char)c;
        }
        else
        {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 0x0F];
        }
    }
    return out;
}

std::string login_url(const std::string &uri, const std::string &query)
{
    std::string target = uri;
    if (!query.empty())
        target += "?" + query;
    return FN_SESSION_LOGIN_PATH "?next=" + url_encode(target);
}

std::string sanitize_next(const std::string &next)
{
    // Must be an absolute path starting with exactly one '/'
    if (next.empty() || next[0] != '/')
        return "/";

    // Reject protocol-relative URLs (start with "//")
    if (next.size() > 1 && next[1] == '/')
        return "/";

    // Reject if it contains carriage return, newline, or backslash
    for (char c : next)
    {
        if (c == '\r' || c == '\n' || c == '\\')
            return "/";
    }

    return next;
}

bool wants_json(const char *accept, const char *requested_with, const char *content_type)
{
    // Check if X-Requested-With: XMLHttpRequest (case-insensitive)
    if (requested_with != nullptr)
    {
        if (strcasecmp(requested_with, "XMLHttpRequest") == 0)
            return true;
    }

    // Check if Accept contains application/json but not text/html
    if (accept != nullptr)
    {
        std::string acc(accept);
        if (acc.find("application/json") != std::string::npos &&
            acc.find("text/html") == std::string::npos)
            return true;
    }

    // Check if Content-Type contains application/json
    if (content_type != nullptr)
    {
        std::string ct(content_type);
        if (ct.find("application/json") != std::string::npos)
            return true;
    }

    return false;
}

bool is_public(const char *uri, const char *query)
{
    if (uri == nullptr)
        return false;

    // Public routes: /login, /logout, /favicon.ico
    if (strcmp(uri, "/login") == 0 || strcmp(uri, "/logout") == 0 || strcmp(uri, "/favicon.ico") == 0)
        return true;

    // /file is public only for non-parsable extensions
    if (strcmp(uri, "/file") == 0)
    {
        if (query == nullptr || query[0] == '\0')
            return false;

        // Extract the extension from the filename in the query.
        // Query format: "path/to/filename.ext"
        std::string q(query);

        // send_file() only prefixes the web root, it does not resolve the path,
        // so a traversing name would reach outside /www/ - e.g. the config file.
        // Never hand one of those out without a session.
        if (q.find("..") != std::string::npos)
            return false;

        size_t last_slash = q.find_last_of('/');
        size_t start = (last_slash != std::string::npos) ? last_slash + 1 : 0;
        size_t last_dot = q.find_last_of('.', std::string::npos);

        if (last_dot == std::string::npos || last_dot <= start)
            return false; // no extension

        std::string ext = q.substr(last_dot + 1);
        return !fnHttpServiceParser::is_parsable(ext.c_str());
    }

    return false;
}

std::string render_login_page(const std::string &next, const std::string &error)
{
    // Escape the 'next' and 'error' values for safe HTML insertion
    std::string escaped_next = FileManager::html_escape(next);
    std::string escaped_error = FileManager::html_escape(error);

    // Laid out like the settings modules in the main web UI (same .settings
    // grid, header, icon panel and footer button), so the login page reads as
    // part of the same interface. core.css carries all of it.
    std::string page =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>FujiNet - Log In</title>"
        "<link rel=\"stylesheet\" href=\"/file?css/core.css\">"
        "<style>"
        ".loginerr{color:#c00;padding-top:0.5rem;}"
        "</style></head><body><div class=\"wrapper\">"
        "<div class=\"headerlogo\"><img src=\"/file?logo.svg\" alt=\"FujiNet\" style=\"width:100%\"></div>"
        "<div class=\"module-container\"><div class=\"module\">"
        "<form method=\"post\" action=\"/login\">"
        "<input type=\"hidden\" name=\"next\" value=\"" + escaped_next + "\">"
        "<div class=\"settings\">"
        "<div class=\"settings-header\">DEVICE<span class=\"logowob\"></span>PASSWORD</div>"
        "<div class=\"settings-left\"><div class=\"svgicon\">"
        "<svg width=\"90%\" height=\"90%\" viewBox=\"0 0 64 64\" xmlns=\"http://www.w3.org/2000/svg\">"
        "<rect x=\"12\" y=\"28\" width=\"40\" height=\"30\" rx=\"4\" fill=\"#f3a933\" stroke=\"#000\" stroke-width=\"3\"/>"
        "<path d=\"M22 28v-8a10 10 0 0 1 20 0v8\" fill=\"none\" stroke=\"#000\" stroke-width=\"5\"/>"
        "<circle cx=\"32\" cy=\"41\" r=\"4.5\"/>"
        "<rect x=\"30\" y=\"43\" width=\"4\" height=\"9\"/>"
        "</svg></div></div>"
        "<div class=\"settings-content settings-45-55\">"
        "<div class=\"set\">"
        "<div class=\"settings-label\"><label for=\"password\">Device password</label></div>"
        "<div class=\"settings-value\">"
        "<input type=\"password\" id=\"password\" name=\"password\" autocomplete=\"current-password\" autofocus required>"
        "</div></div>"
        "<hr>"
        "<div class=\"settings-text\">"
        "<div>This FujiNet is protected by a device password.</div>";

    if (!error.empty())
        page += "<div class=\"loginerr\">" + escaped_error + "</div>";

    page += "</div></div>"
            "<div class=\"settings-footer\"><div class=\"save-button\">"
            "<button type=\"submit\">Log In</button>"
            "</div></div>"
            "</div></form></div></div></div></body></html>";

    return page;
}

const char *json_unauthorized()
{
    return "{\"error\":\"auth required\",\"login\":\"/login\"}";
}

} // namespace fnSession
