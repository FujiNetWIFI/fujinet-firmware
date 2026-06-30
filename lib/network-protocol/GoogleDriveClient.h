#ifndef GOOGLEDRIVECLIENT_H
#define GOOGLEDRIVECLIENT_H

#include <cJSON.h>
#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>

/**
 * Shared Google Drive REST + OAuth2 helper, used by both the GDRIVE:// network
 * protocol adapter and the Google Drive host-slot filesystem. Handles token
 * refresh (via the relay, keeping client_secret server-side; tokens persist in
 * fnConfig), the Drive v3 REST verbs, and path->id resolution. Content
 * streaming is left to each caller via access_token().
 */

// Google Drive v3 REST endpoints.
#define GDRIVE_FILES_URL     "https://www.googleapis.com/drive/v3/files"
#define GDRIVE_UPLOAD_URL    "https://www.googleapis.com/upload/drive/v3/files"
#define GDRIVE_FIELDS        "id,name,size,mimeType,trashed"
#define GDRIVE_FOLDER_MIME   "application/vnd.google-apps.folder"

class GoogleDriveClient
{
public:
    GoogleDriveClient() = default;

    // Ensure a valid access token (refresh via relay if needed).
    bool ensure_access_token();

    // Cached access token (valid after ensure_access_token()).
    const std::string &access_token() const { return _access_token; }

    // Forget the cached access token (refresh token is kept).
    void clear_token() { _access_token.clear(); }

    // Synchronous GET; returns the response body, or "" on error.
    std::string api_get(const std::string &url);

    // Synchronous POST; returns the response body, or "" on error.
    std::string api_post(const std::string &url,
                         const std::string &body,
                         const std::string &content_type);

    // Synchronous DELETE; true on 200/204.
    bool api_delete(const std::string &url);

    // Resolve a path to a Drive file id; "" on failure, "root" if empty.
    std::string resolve_path(const std::string &path);

    // Find a child by name in folder_id; is_folder restricts to folders. "" if none.
    std::string find_child(const std::string &folder_id,
                           const std::string &name,
                           bool is_folder);

    // Create a folder `name` under parent_id. Returns its id.
    std::string create_folder(const std::string &parent_id,
                              const std::string &name);

    // Upload total_len content bytes (pulled in chunks from read_chunk: returns
    // bytes read, 0=EOF, <0=error) via multipart, without buffering the whole
    // file. Updates file_id if set, else creates `name` under parent_id.
    // Returns the new Drive file id, or "".
    std::string upload_stream(const std::string &parent_id,
                              const std::string &name,
                              const std::string &file_id,
                              size_t total_len,
                              const std::function<int(uint8_t *, int)> &read_chunk);

    // URL-encode a string (RFC 3986 unreserved characters pass through).
    static std::string url_encode(const std::string &s);

    // Extract a string field from a cJSON object; returns "" if absent.
    static std::string json_str(cJSON *obj, const char *key);

private:
    // Refresh the access token via the relay and persist it.
    bool refresh_access_token();

    std::string _access_token;
};

#endif /* GOOGLEDRIVECLIENT_H */
