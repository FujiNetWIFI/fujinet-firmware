/**
 * NetworkProtocolGDRIVE
 *
 * Google Drive protocol adapter for FujiNet.
 *
 * The Drive REST calls and OAuth2 token handling live in the shared
 * GoogleDriveClient (_gdrive); this class implements the NetworkProtocolFS
 * surface (open/read/write/dir/del/mkdir/rmdir) on top of it.
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

// ─── NetworkProtocolFS overrides ─────────────────────────────────────────────

fujiError_t NetworkProtocolGDRIVE::mount(PeoplesUrlParser *url)
{
    Debug_printf("NetworkProtocolGDRIVE::mount(%s)\r\n", url->url.c_str());

    if (!_gdrive.ensure_access_token())
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
    _gdrive.clear_token();
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolGDRIVE::stat()
{
    std::string id = _gdrive.resolve_path(opened_url->path);
    if (id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    std::string url = std::string(GDRIVE_FILES_URL) + "/" + id + "?fields=size";
    std::string resp = _gdrive.api_get(url);
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
        _parent_folder_id = _gdrive.resolve_path(parent_path);
        if (_parent_folder_id.empty())
            _parent_folder_id = "root";

        // If appending, locate the existing file and download its current content
        if (streamMode == ACCESS_MODE::APPEND)
        {
            _file_id = _gdrive.find_child(_parent_folder_id, filename, false);
            if (!_file_id.empty())
            {
                std::string dl_url = std::string(GDRIVE_FILES_URL) + "/" +
                                     _file_id + "?alt=media";
                std::string existing = _gdrive.api_get(dl_url);
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
    _file_id = _gdrive.resolve_path(opened_url->path);
    if (_file_id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Populate fileSize from metadata
    {
        std::string url = std::string(GDRIVE_FILES_URL) + "/" + _file_id +
                          "?fields=size";
        std::string resp = _gdrive.api_get(url);
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
    _http.set_header("Authorization", ("Bearer " + _gdrive.access_token()).c_str());
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

        // Upload the buffered bytes via the shared client (streamed).
        size_t off = 0;
        std::string id = _gdrive.upload_stream(
            _parent_folder_id, filename, _file_id, _write_buf.size(),
            [this, &off](uint8_t *buf, int want) -> int {
                size_t remaining = _write_buf.size() - off;
                int n = (int)((remaining < (size_t)want) ? remaining : (size_t)want);
                if (n > 0) { memcpy(buf, _write_buf.data() + off, n); off += n; }
                return n;
            });

        if (id.empty())
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

    std::string node_id = _gdrive.resolve_path(dir_path);
    if (node_id.empty())
        node_id = "root";

    _parent_folder_id = node_id;

    // Determine whether the resolved node is a folder or a plain file.
    // "root" is always a folder; everything else needs a metadata fetch.
    bool is_folder = (node_id == "root");
    std::string meta_resp;

    if (!is_folder)
    {
        meta_resp = _gdrive.api_get(std::string(GDRIVE_FILES_URL) + "/" + node_id +
                                    "?fields=" GDRIVE_FIELDS);
        if (meta_resp.empty())
        {
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }
        cJSON *meta = cJSON_Parse(meta_resp.c_str());
        if (meta)
        {
            is_folder = (GoogleDriveClient::json_str(meta, "mimeType") == GDRIVE_FOLDER_MIME);
            cJSON_Delete(meta);
        }
    }

    if (_dir_json)
        cJSON_Delete(_dir_json);

    if (is_folder)
    {
        // List the children of this folder.
        std::string q = "'" + node_id + "' in parents and trashed=false";
        std::string resp = _gdrive.api_get(std::string(GDRIVE_FILES_URL) +
                                   "?q=" + GoogleDriveClient::url_encode(q) +
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

        std::string name = GoogleDriveClient::json_str(entry, "name");
        std::string mime = GoogleDriveClient::json_str(entry, "mimeType");
        std::string size_str = GoogleDriveClient::json_str(entry, "size");

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

    std::string id = _gdrive.resolve_path(url->path);
    if (id.empty())
    {
        error = NDEV_STATUS::FILE_NOT_FOUND;
        return FUJI_ERROR::UNSPECIFIED;
    }

    bool ok = _gdrive.api_delete(std::string(GDRIVE_FILES_URL) + "/" + id);
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

    std::string parent_id = _gdrive.resolve_path(parent_path);
    if (parent_id.empty()) parent_id = "root";

    std::string id = _gdrive.create_folder(parent_id, new_name);
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
