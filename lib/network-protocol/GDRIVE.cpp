/**
 * NetworkProtocolGDRIVE
 *
 * Google Drive protocol adapter for FujiNet.
 */

#include "GDRIVE.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <algorithm>

#include "../../include/debug.h"
#include "../config/fnConfig.h"
#include "status_error_codes.h"
#include "utils.h"


// Google OAuth2 / Drive API endpoints
// Token refresh goes through the relay so the client_secret stays server-side.
#define GDRIVE_RELAY_REFRESH_URL "https://auth.fujinet.online/gdrive-refresh"
#define GDRIVE_FILES_URL     "https://www.googleapis.com/drive/v3/files"
#define GDRIVE_UPLOAD_URL    "https://www.googleapis.com/upload/drive/v3/files"
#define GDRIVE_FIELDS        "id,name,size,mimeType,trashed"
#define GDRIVE_FOLDER_MIME   "application/vnd.google-apps.folder"

// ─── construction ────────────────────────────────────────────────────────────

NetworkProtocolGDRIVE::NetworkProtocolGDRIVE(std::string *rx_buf,
                                             std::string *tx_buf,
                                             std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = false;
    delete_implemented = true;
    mkdir_implemented  = true;
    rmdir_implemented  = true;
    Debug_printf("NetworkProtocolGDRIVE::ctor\r\n");
}

NetworkProtocolGDRIVE::~NetworkProtocolGDRIVE()
{
    Debug_printf("NetworkProtocolGDRIVE::dtor\r\n");
    if (_dir_json)
    {
        cJSON_Delete(_dir_json);
        _dir_json = nullptr;
    }
}

// ─── helpers ─────────────────────────────────────────────────────────────────

std::string NetworkProtocolGDRIVE::url_encode(const std::string &s)
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

std::string NetworkProtocolGDRIVE::json_str(cJSON *obj, const char *key)
{
    if (!obj) return "";
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item)) return "";
    return item->valuestring ? item->valuestring : "";
}

// ─── OAuth2 token management ─────────────────────────────────────────────────

bool NetworkProtocolGDRIVE::refresh_access_token()
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

bool NetworkProtocolGDRIVE::ensure_access_token()
{
    long now = (long)time(nullptr);
    // Refresh 60 seconds before actual expiry to avoid clock skew issues
    if (now >= Config.get_gdrive_token_expiry() - 60)
        return refresh_access_token();

    _access_token = Config.get_gdrive_access_token();
    return !_access_token.empty();
}

// ─── HTTP helpers (ESP32 only) ────────────────────────────────────────────────

#ifdef ESP_PLATFORM

std::string NetworkProtocolGDRIVE::api_get(const std::string &url)
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

std::string NetworkProtocolGDRIVE::api_post(const std::string &url,
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

bool NetworkProtocolGDRIVE::api_delete(const std::string &url)
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

std::string NetworkProtocolGDRIVE::api_get(const std::string &url)
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

std::string NetworkProtocolGDRIVE::api_post(const std::string &url,
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

bool NetworkProtocolGDRIVE::api_delete(const std::string &url)
{
    mgHttpClient http;
    if (!http.begin(url)) return false;
    http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = http.DELETE();
    return (status == 200 || status == 204);
}

#endif /* ESP_PLATFORM */

// ─── path resolution ──────────────────────────────────────────────────────────

std::string NetworkProtocolGDRIVE::find_child(const std::string &folder_id,
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

std::string NetworkProtocolGDRIVE::create_folder(const std::string &parent_id,
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

std::string NetworkProtocolGDRIVE::resolve_path(const std::string &path)
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

// ─── NetworkProtocolFS overrides ─────────────────────────────────────────────

fujiError_t NetworkProtocolGDRIVE::mount(PeoplesUrlParser *url)
{
    Debug_printf("NetworkProtocolGDRIVE::mount(%s)\r\n", url->url.c_str());

    if (!ensure_access_token())
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::umount()
{
    _file_id.clear();
    _parent_folder_id.clear();
    _access_token.clear();
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::stat()
{
    std::string id = resolve_path(opened_url->path);
    if (id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string url = std::string(GDRIVE_FILES_URL) + "/" + id + "?fields=size";
    std::string resp = api_get(url);
    if (resp.empty())
        return FUJI_ERROR::UNSPECIFIED;

    cJSON *j = cJSON_Parse(resp.c_str());
    if (!j) return FUJI_ERROR::UNSPECIFIED;

    cJSON *sz = cJSON_GetObjectItemCaseSensitive(j, "size");
    fileSize = (sz && cJSON_IsString(sz)) ? atoi(sz->valuestring) : 0;
    cJSON_Delete(j);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::open_file_handle()
{
    Debug_printf("NetworkProtocolGDRIVE::open_file_handle() mode=%d\r\n",
                 (int)streamMode);

    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        // For writes we buffer everything; resolve the parent folder now.
        std::string parent_path = dir.empty() ? "/" : dir;
        _parent_folder_id = resolve_path(parent_path);
        if (_parent_folder_id.empty())
            _parent_folder_id = "root";

        // If appending, locate the existing file and download its current content
        if (streamMode == ACCESS_MODE::APPEND)
        {
            _file_id = find_child(_parent_folder_id, filename, false);
            if (!_file_id.empty())
            {
                std::string dl_url = std::string(GDRIVE_FILES_URL) + "/" +
                                     _file_id + "?alt=media";
                std::string existing = api_get(dl_url);
                _write_buf.assign(existing.begin(), existing.end());
            }
        }
        else
        {
            _write_buf.clear();
        }
        return FUJI_ERROR::NONE;
    }

    // READ / READWRITE: resolve the file ID and open a streaming GET
    _file_id = resolve_path(opened_url->path);
    if (_file_id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Populate fileSize from metadata
    {
        std::string url = std::string(GDRIVE_FILES_URL) + "/" + _file_id +
                          "?fields=size";
        std::string resp = api_get(url);
        if (!resp.empty())
        {
            cJSON *j = cJSON_Parse(resp.c_str());
            if (j)
            {
                cJSON *sz = cJSON_GetObjectItemCaseSensitive(j, "size");
                fileSize = (sz && cJSON_IsString(sz)) ? atoi(sz->valuestring) : 0;
                cJSON_Delete(j);
            }
        }
    }

    std::string dl_url = std::string(GDRIVE_FILES_URL) + "/" +
                         _file_id + "?alt=media";
    if (!_http.begin(dl_url))
    {
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _http.set_header("Authorization", ("Bearer " + _access_token).c_str());
    int status = _http.GET();
    if (status < 200 || status >= 300)
    {
        Debug_printf("GDRIVE::open_file_handle GET %d\r\n", status);
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::read_file_handle(uint8_t *buf,
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

fujiError_t NetworkProtocolGDRIVE::write_file_handle(uint8_t *buf,
                                                       unsigned short len)
{
    if (_write_buf.size() + len > WRITE_BUF_LIMIT)
    {
        Debug_printf("GDRIVE: write buffer limit reached\r\n");
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }
    _write_buf.insert(_write_buf.end(), buf, buf + len);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::close_file_handle()
{
    if (streamMode == ACCESS_MODE::WRITE || streamMode == ACCESS_MODE::APPEND)
    {
        if (_write_buf.empty())
            return FUJI_ERROR::NONE;

        // Build a multipart/related body
        const std::string boundary = "fuji_gdrive_boundary";
        std::string meta = "{\"name\":\"" + filename + "\"";
        if (!_parent_folder_id.empty() && _file_id.empty())
            meta += ",\"parents\":[\"" + _parent_folder_id + "\"]";
        meta += "}";

        std::string body;
        body.reserve(256 + _write_buf.size());
        body += "--" + boundary + "\r\n";
        body += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
        body += meta + "\r\n";
        body += "--" + boundary + "\r\n";
        body += "Content-Type: application/octet-stream\r\n\r\n";
        body.append((char *)_write_buf.data(), _write_buf.size());
        body += "\r\n--" + boundary + "--";

        std::string ct = "multipart/related; boundary=" + boundary;

        std::string url;
        if (_file_id.empty())
        {
            // New file
            url = std::string(GDRIVE_UPLOAD_URL) +
                  "?uploadType=multipart&fields=id";
        }
        else
        {
            // Update existing file
            url = std::string(GDRIVE_UPLOAD_URL) + "/" + _file_id +
                  "?uploadType=multipart&fields=id";
        }

        std::string resp = api_post(url, body, ct);
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

fujiError_t NetworkProtocolGDRIVE::open_dir_handle()
{
    Debug_printf("NetworkProtocolGDRIVE::open_dir_handle() path=%s\r\n",
                 opened_url->path.c_str());

    // Strip trailing wildcard so "GDRIVE:///folder/*.*" lists that folder.
    // resolve_path would otherwise hunt for a child literally named "*.*".
    std::string dir_path = opened_url->path;
    {
        size_t slash = dir_path.rfind('/');
        if (slash != std::string::npos)
        {
            std::string leaf = dir_path.substr(slash + 1);
            if (leaf.find('*') != std::string::npos ||
                leaf.find('?') != std::string::npos)
                dir_path = dir_path.substr(0, slash);
        }
    }

    std::string node_id = resolve_path(dir_path);
    if (node_id.empty())
        node_id = "root";

    _parent_folder_id = node_id;

    // Determine whether the resolved node is a folder or a plain file.
    // "root" is always a folder; everything else needs a metadata fetch.
    bool is_folder = (node_id == "root");
    std::string meta_resp;

    if (!is_folder)
    {
        meta_resp = api_get(std::string(GDRIVE_FILES_URL) + "/" + node_id +
                            "?fields=" GDRIVE_FIELDS);
        if (meta_resp.empty())
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        cJSON *meta = cJSON_Parse(meta_resp.c_str());
        if (meta)
        {
            is_folder = (json_str(meta, "mimeType") == GDRIVE_FOLDER_MIME);
            cJSON_Delete(meta);
        }
    }

    if (_dir_json)
        cJSON_Delete(_dir_json);

    if (is_folder)
    {
        // List the children of this folder.
        std::string q = "'" + node_id + "' in parents and trashed=false";
        std::string resp = api_get(std::string(GDRIVE_FILES_URL) +
                                   "?q=" + url_encode(q) +
                                   "&fields=files(" GDRIVE_FIELDS ")"
                                   "&pageSize=1000"
                                   "&orderBy=name");
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
        _dir_items = cJSON_GetObjectItemCaseSensitive(_dir_json, "files");
    }
    else
    {
        // The path names a single file — synthesise a one-entry listing so the
        // caller sees exactly that file (same JSON shape as a real directory response).
        _dir_json = cJSON_CreateObject();
        cJSON *arr = cJSON_CreateArray();
        cJSON *entry = cJSON_Parse(meta_resp.c_str());
        if (entry)
            cJSON_AddItemToArray(arr, entry);
        cJSON_AddItemToObject(_dir_json, "files", arr);
        _dir_items = arr;
    }

    _dir_item_idx = 0;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::read_dir_entry(char *buf, unsigned short len)
{
    if (!_dir_items)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Skip entries until we find one that isn't trashed
    while (_dir_item_idx < cJSON_GetArraySize(_dir_items))
    {
        cJSON *entry = cJSON_GetArrayItem(_dir_items, _dir_item_idx++);
        if (!entry) continue;

        cJSON *trashed = cJSON_GetObjectItemCaseSensitive(entry, "trashed");
        if (trashed && cJSON_IsTrue(trashed)) continue;

        std::string name = json_str(entry, "name");
        std::string mime = json_str(entry, "mimeType");
        std::string size_str = json_str(entry, "size");

        strncpy(buf, name.c_str(), len - 1);
        buf[len - 1] = '\0';

        is_directory = (mime == GDRIVE_FOLDER_MIME);
        fileSize = size_str.empty() ? 0 : atoi(size_str.c_str());
        mode = 0755;
        return FUJI_ERROR::NONE;
    }

    error = NDEV_STATUS::END_OF_FILE;
    return FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolGDRIVE::close_dir_handle()
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

// ─── file operations (del / mkdir / rmdir) ────────────────────────────────────

fujiError_t NetworkProtocolGDRIVE::del(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    std::string id = resolve_path(url->path);
    if (id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    bool ok = api_delete(std::string(GDRIVE_FILES_URL) + "/" + id);
    umount();
    return ok ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolGDRIVE::mkdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    // parent = everything before the last '/'
    std::string path = url->path;
    size_t slash = path.find_last_of('/');
    std::string parent_path = (slash != std::string::npos)
                                  ? path.substr(0, slash)
                                  : "/";
    std::string new_name = (slash != std::string::npos)
                               ? path.substr(slash + 1)
                               : path;

    std::string parent_id = resolve_path(parent_path);
    if (parent_id.empty()) parent_id = "root";

    std::string id = create_folder(parent_id, new_name);
    umount();
    return id.empty() ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::rmdir(PeoplesUrlParser *url)
{
    return del(url);
}

void NetworkProtocolGDRIVE::fserror_to_error()
{
    error = NDEV_STATUS::GENERAL;
}
