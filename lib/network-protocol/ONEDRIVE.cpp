/**
 * NetworkProtocolONEDRIVE
 *
 * Microsoft OneDrive protocol adapter for FujiNet.
 */

#include "ONEDRIVE.h"

#include <cstring>
#include <ctime>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "status_error_codes.h"
#include "utils.h"


// Microsoft Graph / OneDrive endpoints.
// Token refresh goes through the relay so the client_secret stays server-side.
#define ONEDRIVE_RELAY_REFRESH_URL "https://auth.fujinet.online/onedrive-refresh"
#define ONEDRIVE_GRAPH_ROOT        "https://graph.microsoft.com/v1.0/me/drive"
#define ONEDRIVE_DIR_SELECT        "name,size,folder,file"

// ─── construction ────────────────────────────────────────────────────────────

NetworkProtocolONEDRIVE::NetworkProtocolONEDRIVE(std::string *rx_buf,
                                                 std::string *tx_buf,
                                                 std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    // Graph makes rename a trivial PATCH, so unlike GDRIVE we support it.
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented  = true;
    rmdir_implemented  = true;
    Debug_printf("NetworkProtocolONEDRIVE::ctor\r\n");
}

NetworkProtocolONEDRIVE::~NetworkProtocolONEDRIVE()
{
    Debug_printf("NetworkProtocolONEDRIVE::dtor\r\n");
    if (_dir_json)
    {
        cJSON_Delete(_dir_json);
        _dir_json = nullptr;
    }
}

// ─── helpers ─────────────────────────────────────────────────────────────────

std::string NetworkProtocolONEDRIVE::url_encode_path(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        // Leave path separators and RFC 3986 unreserved characters intact.
        if (c == '/' || isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
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

std::string NetworkProtocolONEDRIVE::json_str(cJSON *obj, const char *key)
{
    if (!obj) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return "";
    return item->valuestring ? item->valuestring : "";
}

std::string NetworkProtocolONEDRIVE::item_url(const std::string &path,
                                              const std::string &action)
{
    // Normalise: drop leading and trailing slashes. Empty result == drive root.
    size_t b = path.find_first_not_of('/');
    size_t e = path.find_last_not_of('/');
    std::string p = (b == std::string::npos) ? "" : path.substr(b, e - b + 1);

    std::string url = ONEDRIVE_GRAPH_ROOT;
    if (p.empty())
    {
        // Root: .../root, .../root/children  (no colon form)
        url += "/root";
        if (!action.empty())
            url += "/" + action;
    }
    else
    {
        // Path-addressed: .../root:/<encoded path>[:/<action>]
        url += "/root:/" + url_encode_path(p);
        if (!action.empty())
            url += ":/" + action;
    }
    return url;
}

// ─── OAuth2 token management ─────────────────────────────────────────────────

bool NetworkProtocolONEDRIVE::refresh_access_token()
{
    std::string refresh_token = Config.get_onedrive_refresh_token();

    if (refresh_token.empty())
    {
        Debug_printf("ONEDRIVE: no refresh token — user must re-authorize\r\n");
        return false;
    }

    std::string body = "refresh_token=" + url_encode_path(refresh_token);

    std::string resp = api_post(ONEDRIVE_RELAY_REFRESH_URL, body,
                                "application/x-www-form-urlencoded");
    if (resp.empty())
        return false;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j)
    {
        Debug_printf("ONEDRIVE: failed to parse token response\r\n");
        return false;
    }

    std::string token = json_str(j, "access_token");
    cJSON *expires_in = cJSON_GetObjectItemCaseSensitive(j, "expires_in");
    long ttl = (expires_in && cJSON_IsNumber(expires_in))
                   ? (long)expires_in->valuedouble
                   : 3600;
    // Microsoft may issue a rotated refresh_token; persist it if present.
    std::string new_refresh = json_str(j, "refresh_token");
    cJSON_Delete(j);

    if (token.empty())
    {
        Debug_printf("ONEDRIVE: token refresh returned no access_token\r\n");
        return false;
    }

    long expiry = (long)time(nullptr) + ttl;
    Config.store_onedrive_access_token(token);
    Config.store_onedrive_token_expiry(expiry);
    if (!new_refresh.empty())
        Config.store_onedrive_refresh_token(new_refresh);
    Config.save();

    _access_token = token;
    Debug_printf("ONEDRIVE: access token refreshed, expires in %ld s\r\n", ttl);
    return true;
}

bool NetworkProtocolONEDRIVE::ensure_access_token()
{
    long now = (long)time(nullptr);
    // Refresh 60 seconds before actual expiry to avoid clock skew issues
    if (now >= Config.get_onedrive_token_expiry() - 60)
        return refresh_access_token();

    _access_token = Config.get_onedrive_access_token();
    return !_access_token.empty();
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────

#ifdef ESP_PLATFORM

std::string NetworkProtocolONEDRIVE::api_get(const std::string &url)
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
        Debug_printf("ONEDRIVE api_get: open failed for %s\r\n", url.c_str());
        return "";
    }
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("ONEDRIVE api_get: HTTP %d for %s\r\n", status, url.c_str());
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

std::string NetworkProtocolONEDRIVE::api_post(const std::string &url,
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
        Debug_printf("ONEDRIVE api_post: open failed for %s\r\n", url.c_str());
        return "";
    }
    esp_http_client_write(h, body.c_str(), wlen);
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("ONEDRIVE api_post: HTTP %d for %s\r\n", status, url.c_str());
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

std::string NetworkProtocolONEDRIVE::api_put(const std::string &url,
                                             const uint8_t *data, size_t len,
                                             const std::string &content_type)
{
    esp_http_client_config_t cfg = {};
    cfg.url        = url.c_str();
    cfg.timeout_ms = 30000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return "";

    esp_http_client_set_method(h, HTTP_METHOD_PUT);
    std::string auth = "Bearer " + _access_token;
    esp_http_client_set_header(h, "Authorization", auth.c_str());
    esp_http_client_set_header(h, "Content-Type", content_type.c_str());

    if (esp_http_client_open(h, (int)len) != ESP_OK) {
        esp_http_client_cleanup(h);
        Debug_printf("ONEDRIVE api_put: open failed for %s\r\n", url.c_str());
        return "";
    }
    size_t off = 0;
    while (off < len) {
        int w = esp_http_client_write(h, (const char *)data + off, (int)(len - off));
        if (w <= 0) break;
        off += w;
    }
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("ONEDRIVE api_put: HTTP %d for %s\r\n", status, url.c_str());
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

std::string NetworkProtocolONEDRIVE::api_patch(const std::string &url,
                                               const std::string &body,
                                               const std::string &content_type)
{
    esp_http_client_config_t cfg = {};
    cfg.url        = url.c_str();
    cfg.timeout_ms = 15000;

    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return "";

    esp_http_client_set_method(h, HTTP_METHOD_PATCH);
    std::string auth = "Bearer " + _access_token;
    esp_http_client_set_header(h, "Authorization", auth.c_str());
    esp_http_client_set_header(h, "Content-Type", content_type.c_str());

    int wlen = (int)body.size();
    if (esp_http_client_open(h, wlen) != ESP_OK) {
        esp_http_client_cleanup(h);
        Debug_printf("ONEDRIVE api_patch: open failed for %s\r\n", url.c_str());
        return "";
    }
    esp_http_client_write(h, body.c_str(), wlen);
    esp_http_client_fetch_headers(h);

    int status = esp_http_client_get_status_code(h);
    if (status < 200 || status >= 300) {
        Debug_printf("ONEDRIVE api_patch: HTTP %d for %s\r\n", status, url.c_str());
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

bool NetworkProtocolONEDRIVE::api_delete(const std::string &url)
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

std::string NetworkProtocolONEDRIVE::api_get(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("ONEDRIVE api_get: begin failed for %s\r\n", url.c_str()); return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = http.GET();
    if (status < 200 || status >= 300) { Debug_printf("ONEDRIVE api_get: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string body;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) body.append((char *)buf, n);
    return body;
}

std::string NetworkProtocolONEDRIVE::api_post(const std::string &url,
                                              const std::string &body,
                                              const std::string &content_type)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("ONEDRIVE api_post: begin failed for %s\r\n", url.c_str()); return ""; }
    if (!_access_token.empty())
        http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());
    int status = http.POST(body.c_str(), (int)body.size());
    if (status < 200 || status >= 300) { Debug_printf("ONEDRIVE api_post: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string resp;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) resp.append((char *)buf, n);
    return resp;
}

std::string NetworkProtocolONEDRIVE::api_put(const std::string &url,
                                             const uint8_t *data, size_t len,
                                             const std::string &content_type)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("ONEDRIVE api_put: begin failed for %s\r\n", url.c_str()); return ""; }
    http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());
    int status = http.PUT((const char *)data, (int)len);
    if (status < 200 || status >= 300) { Debug_printf("ONEDRIVE api_put: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string resp;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) resp.append((char *)buf, n);
    return resp;
}

std::string NetworkProtocolONEDRIVE::api_patch(const std::string &url,
                                               const std::string &body,
                                               const std::string &content_type)
{
    mgHttpClient http;
    if (!http.begin(url)) { Debug_printf("ONEDRIVE api_patch: begin failed for %s\r\n", url.c_str()); return ""; }
    http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    http.set_header("Content-Type", content_type.c_str());
    int status = http.PATCH(body.c_str(), (int)body.size());
    if (status < 200 || status >= 300) { Debug_printf("ONEDRIVE api_patch: HTTP %d for %s\r\n", status, url.c_str()); return ""; }
    std::string resp;
    uint8_t buf[512]; int n;
    while ((n = http.read(buf, sizeof(buf))) > 0) resp.append((char *)buf, n);
    return resp;
}

bool NetworkProtocolONEDRIVE::api_delete(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) return false;
    http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = http.DELETE();
    return (status == 200 || status == 204);
}

#endif /* ESP_PLATFORM */

// ─── NetworkProtocolFS overrides ─────────────────────────────────────────────

fujiError_t NetworkProtocolONEDRIVE::mount(PeoplesUrlParser *url)
{
    Debug_printf("NetworkProtocolONEDRIVE::mount(%s)\r\n", url->url.c_str());

    if (!ensure_access_token())
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::umount()
{
    _item_path.clear();
    _access_token.clear();
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::stat()
{
    std::string resp = api_get(item_url(opened_url->path, "") + "?$select=size");
    if (resp.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return FUJI_ERROR::UNSPECIFIED;

    cJSON *sz = cJSON_GetObjectItemCaseSensitive(j, "size");
    fileSize = (sz && cJSON_IsNumber(sz)) ? (int)sz->valuedouble : 0;
    cJSON_Delete(j);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::open_file_handle()
{
    Debug_printf("NetworkProtocolONEDRIVE::open_file_handle() mode=%d\r\n",
                 (int)streamMode);

    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        // For writes we buffer everything and upload on close.
        _item_path = opened_url->path;

        if (streamMode == ACCESS_MODE::APPEND)
        {
            // Load existing content so appended bytes follow it.
            std::string existing = api_get(item_url(_item_path, "content"));
            _write_buf.assign(existing.begin(), existing.end());
        }
        else
        {
            _write_buf.clear();
        }
        return FUJI_ERROR::NONE;
    }

    // READ / READWRITE: fetch size, then open a streaming download.
    _item_path = opened_url->path;

    {
        std::string resp = api_get(item_url(_item_path, "") + "?$select=size");
        if (resp.empty())
        {
            error = NDEV_STATUS::FILE_NOT_FOUND;
            return FUJI_ERROR::UNSPECIFIED;
        }
        cJSON *j = cJSON_Parse(resp.c_str());
        if (j)
        {
            cJSON *sz = cJSON_GetObjectItemCaseSensitive(j, "size");
            fileSize = (sz && cJSON_IsNumber(sz)) ? (int)sz->valuedouble : 0;
            cJSON_Delete(j);
        }
    }

    // GET :/content 302-redirects to a pre-authenticated download URL; the
    // HTTP client follows the redirect automatically.
    std::string dl_url = item_url(_item_path, "content");
    if (!_http.begin(dl_url))
    {
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = _http.GET();
    if (status < 200 || status >= 300)
    {
        Debug_printf("ONEDRIVE::open_file_handle GET %d\r\n", status);
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::read_file_handle(uint8_t *buf,
                                                      unsigned short len)
{
    // Guard: base class read_file() decrements fileSize by len after we return,
    // so we must NOT also decrement here — that would double-count and wrap the
    // signed int negative, making available() return ~4 GB forever.
    if (fileSize <= 0)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }
    int n = _http.read(buf, len);
    if (n <= 0)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::write_file_handle(uint8_t *buf,
                                                       unsigned short len)
{
    if (_write_buf.size() + len > WRITE_BUF_LIMIT)
    {
        Debug_printf("ONEDRIVE: write buffer limit reached\r\n");
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _write_buf.insert(_write_buf.end(), buf, buf + len);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::close_file_handle()
{
    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        if (_write_buf.empty())
            return FUJI_ERROR::NONE;

        // Simple upload: PUT the buffered bytes to the item's content endpoint.
        std::string resp = api_put(item_url(_item_path, "content"),
                                   _write_buf.data(), _write_buf.size(),
                                   "application/octet-stream");
        if (resp.empty())
        {
            error = NDEV_STATUS::GENERAL;
            return FUJI_ERROR::UNSPECIFIED;
        }
        _write_buf.clear();
        _write_buf.shrink_to_fit();
    }
    else
    {
        _http.close();
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::open_dir_handle()
{
    Debug_printf("NetworkProtocolONEDRIVE::open_dir_handle() path=%s\r\n",
                 opened_url->path.c_str());

    // Strip a trailing wildcard so "ONEDRIVE:///folder/*.*" lists that folder.
    std::string dir_path = opened_url->path;
    _dir_filter.clear();
    {
        size_t slash = dir_path.rfind('/');
        if (slash != std::string::npos)
        {
            std::string leaf = dir_path.substr(slash + 1);
            if (leaf.find('*') != std::string::npos ||
                leaf.find('?') != std::string::npos)
            {
                // "*.*"/"**" mean everything; normalise so dotless names match.
                _dir_filter = (leaf == "*.*" || leaf == "**") ? "*" : leaf;
                dir_path = dir_path.substr(0, slash);
            }
        }
    }

    // Determine whether the resolved node is a folder or a plain file.
    // The drive root is always a folder; everything else needs a metadata fetch.
    size_t nb = dir_path.find_first_not_of('/');
    bool is_root = (nb == std::string::npos);
    bool is_folder = is_root;
    std::string meta_resp;

    if (!is_root)
    {
        meta_resp = api_get(item_url(dir_path, "") + "?$select=" ONEDRIVE_DIR_SELECT);
        if (meta_resp.empty())
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        cJSON *meta = cJSON_Parse(meta_resp.c_str());
        if (meta)
        {
            is_folder = (cJSON_GetObjectItemCaseSensitive(meta, "folder") != nullptr);
            cJSON_Delete(meta);
        }
    }

    if (_dir_json)
        cJSON_Delete(_dir_json);

    if (is_folder)
    {
        std::string resp = api_get(item_url(dir_path, "children") +
                                   "?$select=" ONEDRIVE_DIR_SELECT
                                   "&$top=1000&$orderby=name");
        if (resp.empty())
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        _dir_json = cJSON_Parse(resp.c_str());
        if (!_dir_json)
        {
            error = NDEV_STATUS::GENERAL;
            return FUJI_ERROR::UNSPECIFIED;
        }
        _dir_items = cJSON_GetObjectItemCaseSensitive(_dir_json, "value");
    }
    else
    {
        // The path names a single file — synthesise a one-entry listing so the
        // caller sees exactly that file (same shape as a real children response).
        _dir_json = cJSON_CreateObject();
        cJSON *arr = cJSON_CreateArray();
        cJSON *entry = cJSON_Parse(meta_resp.c_str());
        if (entry)
            cJSON_AddItemToArray(arr, entry);
        cJSON_AddItemToObject(_dir_json, "value", arr);
        _dir_items = arr;
    }

    _dir_item_idx = 0;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::read_dir_entry(char *buf, unsigned short len)
{
    if (!_dir_items)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    while (_dir_item_idx < cJSON_GetArraySize(_dir_items))
    {
        cJSON *entry = cJSON_GetArrayItem(_dir_items, _dir_item_idx++);
        if (!entry) continue;

        std::string name = json_str(entry, "name");

        is_directory = (cJSON_GetObjectItemCaseSensitive(entry, "folder") != nullptr);

        // Filter files by the wildcard; directories always list.
        if (!is_directory && !_dir_filter.empty() &&
            !util_wildcard_match(name.c_str(), _dir_filter.c_str()))
            continue;

        strncpy(buf, name.c_str(), len - 1);
        buf[len - 1] = '\0';

        cJSON *sz = cJSON_GetObjectItemCaseSensitive(entry, "size");
        fileSize = (sz && cJSON_IsNumber(sz)) ? (int)sz->valuedouble : 0;
        mode = 0755;

        return FUJI_ERROR::NONE;
    }

    error = NDEV_STATUS::END_OF_FILE;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolONEDRIVE::close_dir_handle()
{
    if (_dir_json)
    {
        cJSON_Delete(_dir_json);
        _dir_json = nullptr;
        _dir_items = nullptr;
    }
    _dir_item_idx = 0;
    return FUJI_ERROR::NONE;
}

// ─── file operations (del / mkdir / rmdir / rename) ───────────────────────────

fujiError_t NetworkProtocolONEDRIVE::del(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    bool ok = api_delete(item_url(url->path, ""));
    umount();
    return ok ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolONEDRIVE::mkdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    // parent = everything before the last '/'; new folder name = the leaf.
    std::string path = url->path;
    size_t slash = path.find_last_of('/');
    std::string parent_path = (slash != std::string::npos) ? path.substr(0, slash) : "";
    std::string new_name    = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    std::string body = "{\"name\":\"" + new_name + "\","
                       "\"folder\":{},"
                       "\"@microsoft.graph.conflictBehavior\":\"fail\"}";

    std::string resp = api_post(item_url(parent_path, "children"), body,
                                "application/json");
    umount();
    return resp.empty() ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolONEDRIVE::rmdir(PeoplesUrlParser *url)
{
    return del(url);
}

fujiError_t NetworkProtocolONEDRIVE::rename(PeoplesUrlParser *url)
{
    // Base class parses the "old,new" devicespec into filename / destFilename
    // (both carry the same dir prefix — this is an in-place rename).
    if (NetworkProtocolFS::rename(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    std::string new_name = destFilename.substr(destFilename.find_last_of('/') + 1);
    std::string body = "{\"name\":\"" + new_name + "\"}";

    std::string resp = api_patch(item_url(filename, ""), body, "application/json");
    umount();
    return resp.empty() ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

void NetworkProtocolONEDRIVE::fserror_to_error()
{
    error = NDEV_STATUS::GENERAL;
}
