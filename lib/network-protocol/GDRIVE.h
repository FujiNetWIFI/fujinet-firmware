#ifndef NETWORKPROTOCOLGDRIVE_H
#define NETWORKPROTOCOLGDRIVE_H

#include "FS.h"

#include <cJSON.h>
#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#define GDRIVE_HTTP_CLIENT fnHttpClient
#else
#include "../http/mgHttpClient.h"
#define GDRIVE_HTTP_CLIENT mgHttpClient
#endif

/**
 * NetworkProtocolGDRIVE
 *
 * Google Drive protocol adapter for FujiNet.
 *
 * URL format:  GDRIVE:///path/to/file.txt
 *              GDRIVE:///folder/subfolder/file.txt
 *
 * Authentication uses the OAuth2 device authorization grant (RFC 8628).
 * Client ID and secret are stored in fnConfig [GoogleDrive] section.
 * Tokens are refreshed automatically on expiry.
 *
 * Write operations are buffered; the upload happens on close.
 */
class NetworkProtocolGDRIVE : public NetworkProtocolFS
{
public:
    NetworkProtocolGDRIVE(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolGDRIVE();

    NetworkProtocolGDRIVE(const NetworkProtocolGDRIVE &) = delete;
    NetworkProtocolGDRIVE &operator=(const NetworkProtocolGDRIVE &) = delete;

    fujiError_t del(PeoplesUrlParser *url) override;
    fujiError_t mkdir(PeoplesUrlParser *url) override;
    fujiError_t rmdir(PeoplesUrlParser *url) override;

protected:
    fujiError_t open_file_handle() override;
    fujiError_t open_dir_handle() override;
    fujiError_t mount(PeoplesUrlParser *url) override;
    fujiError_t umount() override;
    void fserror_to_error() override;
    fujiError_t read_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t read_dir_entry(char *buf, unsigned short len) override;
    fujiError_t write_file_handle(uint8_t *buf, unsigned short len) override;
    fujiError_t close_file_handle() override;
    fujiError_t close_dir_handle() override;
    fujiError_t stat() override;

private:
    // Resolved Google Drive file/folder IDs for open file/dir
    std::string _file_id;
    std::string _parent_folder_id;

    // Cached access token for this session
    std::string _access_token;

    // Directory listing state
    cJSON *_dir_json = nullptr;
    cJSON *_dir_items = nullptr;
    int _dir_item_idx = 0;

    // Write buffer – data accumulates here until close_file_handle uploads it
    std::vector<uint8_t> _write_buf;

    // Last Google Drive API error message (for debug)
    std::string _last_error;

    // Persistent HTTP client used for streaming file downloads
    GDRIVE_HTTP_CLIENT _http;

    // Ensure _access_token is valid, refreshing via refresh_token if needed.
    bool ensure_access_token();

    // Exchange refresh_token for a new access_token and update fnConfig.
    bool refresh_access_token();

    // Resolve a path string ("/folder/sub/file.txt") to a Drive file ID.
    // Returns empty string on failure.
    std::string resolve_path(const std::string &path);

    // Find a child item (file or folder) by name inside a given folder ID.
    // Pass is_folder=true to restrict to folders.
    std::string find_child(const std::string &folder_id,
                           const std::string &name,
                           bool is_folder);

    // Create a folder with the given name inside parent_id.
    std::string create_folder(const std::string &parent_id, const std::string &name);

    // Perform a synchronous GET and return the response body as a string.
    // Returns empty on HTTP error.
    std::string api_get(const std::string &url);

    // Perform a synchronous POST with the given body and content-type.
    // Returns the response body or empty on error.
    std::string api_post(const std::string &url,
                         const std::string &body,
                         const std::string &content_type);

    // Perform a synchronous DELETE. Returns true on success (204).
    bool api_delete(const std::string &url);

    // URL-encode a string (RFC 3986 unreserved characters are passed through).
    static std::string url_encode(const std::string &s);

    // Extract a string field from a cJSON object; returns "" if absent.
    static std::string json_str(cJSON *obj, const char *key);

    // Maximum bytes to buffer for a single write operation.
    static constexpr size_t WRITE_BUF_LIMIT = 65536;
};

#endif /* NETWORKPROTOCOLGDRIVE_H */
