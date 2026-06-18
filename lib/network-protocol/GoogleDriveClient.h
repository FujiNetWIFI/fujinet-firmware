#ifndef GOOGLEDRIVECLIENT_H
#define GOOGLEDRIVECLIENT_H

#include <cJSON.h>
#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>

/**
 * GoogleDriveClient
 *
 * Shared Google Drive REST helper used by both the GDRIVE:// network protocol
 * adapter (NetworkProtocolGDRIVE) and the Google Drive host-slot filesystem
 * (FileSystemGDrive).
 *
 * Responsibilities:
 *   - OAuth2 access-token management (refresh via the FujiNet relay so the
 *     client_secret never leaves the server). Tokens are persisted in the
 *     fnConfig [GoogleDrive] section.
 *   - Thin synchronous wrappers over the Drive v3 REST API (GET/POST/DELETE).
 *   - Path -> file-id resolution and folder helpers.
 *
 * Streaming downloads/uploads of file *content* are intentionally left to the
 * caller: each consumer owns its own HTTP client for that (the protocol adapter
 * streams to the bus, the filesystem caches to local storage). Callers set the
 * "Authorization: Bearer <token>" header themselves using access_token().
 */

// Google Drive v3 REST endpoints, shared by all consumers.
#define GDRIVE_FILES_URL     "https://www.googleapis.com/drive/v3/files"
#define GDRIVE_UPLOAD_URL    "https://www.googleapis.com/upload/drive/v3/files"
#define GDRIVE_FIELDS        "id,name,size,mimeType,trashed"
#define GDRIVE_FOLDER_MIME   "application/vnd.google-apps.folder"

class GoogleDriveClient
{
public:
    GoogleDriveClient() = default;

    // Ensure _access_token is valid, refreshing via the relay if needed.
    // Returns true if a usable access token is available.
    bool ensure_access_token();

    // The current cached access token (valid after ensure_access_token()).
    const std::string &access_token() const { return _access_token; }

    // Forget the cached access token (the persisted refresh token is kept).
    void clear_token() { _access_token.clear(); }

    // Perform a synchronous GET and return the response body as a string.
    // Returns empty on HTTP error.
    std::string api_get(const std::string &url);

    // Perform a synchronous POST with the given body and content-type.
    // Returns the response body or empty on error.
    std::string api_post(const std::string &url,
                         const std::string &body,
                         const std::string &content_type);

    // Perform a synchronous DELETE. Returns true on success (200/204).
    bool api_delete(const std::string &url);

    // Resolve a path string ("/folder/sub/file.txt") to a Drive file ID.
    // Returns empty string on failure. A bare/empty path resolves to "root".
    std::string resolve_path(const std::string &path);

    // Find a child item (file or folder) by name inside a given folder ID.
    // Pass is_folder=true to restrict to folders. Returns empty if not found.
    std::string find_child(const std::string &folder_id,
                           const std::string &name,
                           bool is_folder);

    // Create a folder with the given name inside parent_id. Returns its id.
    std::string create_folder(const std::string &parent_id,
                              const std::string &name);

    // Upload file content via a multipart/related request, reading the body in
    // chunks from read_chunk() (returns bytes read, 0 = EOF, <0 = error) so the
    // whole file never needs to be buffered in RAM. If file_id is non-empty the
    // existing file is updated; otherwise a new file named `name` is created
    // under parent_id. total_len must be the exact number of content bytes that
    // read_chunk will deliver. Returns the resulting Drive file id, or "".
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
    // Exchange the stored refresh_token for a new access_token (via relay)
    // and persist it to fnConfig.
    bool refresh_access_token();

    // Cached access token for this session.
    std::string _access_token;
};

#endif /* GOOGLEDRIVECLIENT_H */
