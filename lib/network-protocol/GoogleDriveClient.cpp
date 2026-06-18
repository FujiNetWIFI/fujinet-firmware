/**
 * GoogleDriveClient
 *
 * Shared Google Drive REST helper. See GoogleDriveClient.h for the rationale.
 */

#include "GoogleDriveClient.h"

#include <cctype>
#include <cstring>
#include <ctime>
#include <sstream>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#else
#include "../http/mgHttpClient.h"
#endif

#include "../../include/debug.h"
#include "../config/fnConfig.h"

// Token refresh goes through the relay so the client_secret stays server-side.
#define GDRIVE_RELAY_REFRESH_URL "https://auth.fujinet.online/gdrive-refresh"

// ─── helpers ─────────────────────────────────────────────────────────────────

std::string GoogleDriveClient::url_encode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string GoogleDriveClient::json_str(cJSON *obj, const char *key)
{
    if (!obj) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return "";
    return item->valuestring ? item->valuestring : "";
}

// ─── OAuth2 token management ─────────────────────────────────────────────────

bool GoogleDriveClient::refresh_access_token()
{
    std::string refresh_token = Config.get_gdrive_refresh_token();

    if (refresh_token.empty())
    {
        Debug_printf("GDRIVE: no refresh token — user must re-authorize\r\n");
        return false;
    }

    std::string body = "refresh_token=" + url_encode(refresh_token);

    std::string resp = api_post(GDRIVE_RELAY_REFRESH_URL, body,
                                "application/x-www-form-urlencoded");
    if (resp.empty())
        return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j)
    {
        Debug_printf("GDRIVE: failed to parse token response\r\n");
        return false;
    }

    std::string token = json_str(j, "access_token");
    cJSON *expires_in = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    long ttl = (expires_in && cJSON_IsNumber(expires_in))
                   ? (long)expires_in->valuedouble
                   : 3600;
    cJSON_Delete(j);

    if (token.empty())
    {
        Debug_printf("GDRIVE: token refresh returned no access_token\r\n");
        return false;
    }

    long expiry = (long)time(nullptr) + ttl;
    Config.store_gdrive_access_token(token);
    Config.store_gdrive_token_expiry(expiry);
    Config.save();

    _access_token = token;
    Debug_printf("GDRIVE: access token refreshed, expires in %ld s\r\n", ttl);
    return true;
}

bool GoogleDriveClient::ensure_access_token()
{
    long now = (long)time(nullptr);
    // Refresh 60 seconds before actual expiry to avoid clock skew issues
    if (now >= Config.get_gdrive_token_expiry() - 60)
        return refresh_access_token();

    _access_token = Config.get_gdrive_access_token();
    return !_access_token.empty();
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

#ifdef ESP_PLATFORM

std::string GoogleDriveClient::api_get(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url        = url.c_str();
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return "";

    std::string auth = "Bearer " + _access_token;
    esp_http_client_set_header(h, "Authorization", auth.c_str());

    if (esp_http_client_open(h, 0) != ESP_OK) {
        esp_http_client_cleanup(h);
        Debug_printf("GDRIVE api_get: open failed for %s\r\n", url.c_str());
        return "";
    }
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("GDRIVE api_get: HTTP %d for %s\r\n", status, url.c_str());
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        return "";
    }

    std::string body;
    char buf[512];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        body.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return body;
}

std::string GoogleDriveClient::api_post(const std::string &url,
                                        const std::string &body,
                                        const std::string &content_type)
{
    esp_http_client_config_t cfg = {};
    cfg.url        = url.c_str();
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return "";

    esp_http_client_set_method(h, HTTP_METHOD_POST);
    if (!_access_token.empty()) {
        std::string auth = "Bearer " + _access_token;
        esp_http_client_set_header(h, "Authorization", auth.c_str());
    }
    esp_http_client_set_header(h, "Content-Type", content_type.c_str());

    int wlen = (int)body.size();
    if (esp_http_client_open(h, wlen) != ESP_OK) {
        esp_http_client_cleanup(h);
        Debug_printf("GDRIVE api_post: open failed for %s\r\n", url.c_str());
        return "";
    }
    esp_http_client_write(h, body.c_str(), wlen);
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("GDRIVE api_post: HTTP %d for %s\r\n", status, url.c_str());
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        return "";
    }

    std::string resp;
    char buf[512];
    int n;
    while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0)
        resp.append(buf, n);

    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return resp;
}

bool GoogleDriveClient::api_delete(const std::string &url)
{
    esp_http_client_config_t cfg = {};
    cfg.url        = url.c_str();
    cfg.timeout_ms = 10000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return false;

    esp_http_client_set_method(h, HTTP_METHOD_DELETE);
    std::string auth = "Bearer " + _access_token;
    esp_http_client_set_header(h, "Authorization", auth.c_str());

    if (esp_http_client_open(h, 0) != ESP_OK) {
        esp_http_client_cleanup(h);
        return false;
    }
    esp_http_client_fetch_headers(h);
    int status = esp_http_client_get_status_code(h);
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    return (status == 200 || status == 204);
}

#else /* !ESP_PLATFORM */

std::string GoogleDriveClient::api_get(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("GDRIVE api_get: begin failed for %s\r\n", url.c_str()); return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = http.GET();
    if (status < 200 || status >= 300) { Debug_printf("GDRIVE api_get: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string body;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) body.append((char *)buf, n);
    return body;
}

std::string GoogleDriveClient::api_post(const std::string &url,
                                        const std::string &body,
                                        const std::string &content_type)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("GDRIVE api_post: begin failed for %s\r\n", url.c_str()); return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());
    int status = http.POST(body.c_str(), (int)body.size());
    if (status < 200 || status >= 300) { Debug_printf("GDRIVE api_post: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string resp;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) resp.append((char *)buf, n);
    return resp;
}

bool GoogleDriveClient::api_delete(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) return false;
    http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = http.DELETE();
    return (status == 200 || status == 204);
}

#endif /* ESP_PLATFORM */

// ─── path resolution ──────────────────────────────────────────────────────────

std::string GoogleDriveClient::find_child(const std::string &folder_id,
                                          const std::string &name,
                                          bool is_folder)
{
    std::string q = "'" + folder_id + "' in parents"
                    " and name='" + name + "'"
                    " and trashed=false";
    if (is_folder)
        q += " and mimeType='" GDRIVE_FOLDER_MIME "'";

    std::string url = std::string(GDRIVE_FILES_URL) +
                      "?q=" + url_encode(q) +
                      "&fields=files(id,mimeType)&pageSize=1";

    std::string resp = api_get(url);
    if (resp.empty())
        return "";

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return "";

    std::string id;
    cJSON *files = cJSON_GetObjectItemCaseSensitive(j, "files");
    if (files && cJSON_IsArray(files) && cJSON_GetArraySize(files) > 0)
        id = json_str(cJSON_GetArrayItem(files, 0), "id");

    cJSON_Delete(j);
    return id;
}

std::string GoogleDriveClient::create_folder(const std::string &parent_id,
                                             const std::string &name)
{
    std::string body = "{\"name\":\"" + name + "\","
                       "\"mimeType\":\"" GDRIVE_FOLDER_MIME "\","
                       "\"parents\":[\"" + parent_id + "\"]}";

    std::string resp = api_post(std::string(GDRIVE_FILES_URL) + "?fields=id",
                                body, "application/json");
    if (resp.empty()) return "";

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return "";
    std::string id = json_str(j, "id");
    cJSON_Delete(j);
    return id;
}

std::string GoogleDriveClient::resolve_path(const std::string &path)
{
    // Split the path (leading '/' is ignored) into components and walk the tree.
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (!part.empty()) parts.push_back(part);

    if (parts.empty())
        return "root";

    std::string current_id = "root";
    for (size_t i = 0; i < parts.size(); i++)
    {
        bool last = (i == parts.size() - 1);
        // Treat intermediate components as folders; the last one may be a file
        std::string child = find_child(current_id, parts[i], !last);
        if (child.empty())
        {
            // Try again without the folder restriction in case the last
            // component is actually a folder (directory open case)
            if (last)
                child = find_child(current_id, parts[i], true);
            if (child.empty())
            {
                Debug_printf("GDRIVE: not found: %s\r\n", parts[i].c_str());
                return "";
            }
        }
        current_id = child;
    }
    return current_id;
}
