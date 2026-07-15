#ifndef NETWORKPROTOCOLONEDRIVE_H
#define NETWORKPROTOCOLONEDRIVE_H

#include "FS.h"

#include <cJSON.h>
#include <string>
#include <vector>

#ifdef ESP_PLATFORM
#include "../http/fnHttpClient.h"
#define ONEDRIVE_HTTP_CLIENT fnHttpClient
#else
#include "../http/mgHttpClient.h"
#define ONEDRIVE_HTTP_CLIENT mgHttpClient
#endif

/**
 * NetworkProtocolONEDRIVE
 *
 * Microsoft OneDrive protocol adapter for FujiNet.
 *
 * URL format:  ONEDRIVE:///path/to/file.txt
 *              ONEDRIVE:///folder/subfolder/file.txt
 *
 * Authentication uses the OAuth2 authorization-code flow via the shared FujiNet
 * relay (auth.fujinet.online) so the client_secret stays server-side; only the
 * access/refresh tokens live on the device (fnConfig [OneDrive] section).
 * Tokens are refreshed automatically on expiry.
 *
 * Microsoft Graph addresses items by path, so there is no per-component ID walk:
 *   https://graph.microsoft.com/v1.0/me/drive/root:/dir/file.txt:/content
 *
 * Write operations are buffered; the upload happens on close.
 */
class NetworkProtocolONEDRIVE : public NetworkProtocolFS
{
public:
    NetworkProtocolONEDRIVE(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);
    virtual ~NetworkProtocolONEDRIVE();

    NetworkProtocolONEDRIVE(const NetworkProtocolONEDRIVE &) = delete;
    NetworkProtocolONEDRIVE &operator=(const NetworkProtocolONEDRIVE &) = delete;

    fujiError_t del(PeoplesUrlParser *url) override;
    fujiError_t mkdir(PeoplesUrlParser *url) override;
    fujiError_t rmdir(PeoplesUrlParser *url) override;
    fujiError_t rename(PeoplesUrlParser *url) override;

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
    // Cached access token for this session
    std::string _access_token;

    // Full Graph path of the item this session is reading/writing
    std::string _item_path;

    // Directory listing state (Graph returns children in a "value" array)
    cJSON *_dir_json = nullptr;
    cJSON *_dir_items = nullptr;
    int _dir_item_idx = 0;
    // Wildcard applied to files during a listing; empty means no filter.
    std::string _dir_filter;

    // Write buffer – data accumulates here until close_file_handle uploads it
    std::vector<uint8_t> _write_buf;

    // Persistent HTTP client used for streaming file downloads
    ONEDRIVE_HTTP_CLIENT _http;

    // Ensure _access_token is valid, refreshing via refresh_token if needed.
    bool ensure_access_token();

    // Exchange refresh_token for a new access_token (via relay) and update fnConfig.
    bool refresh_access_token();

    // Build a Graph URL for the item at path with an action suffix.
    // action: "" (metadata), "content", or "children".
    // The drive root (empty/"/" path) uses the ".../root<...>" form, everything
    // else uses the ".../root:/<encoded path>:/<...>" form.
    std::string item_url(const std::string &path, const std::string &action);

    // Perform a synchronous GET and return the response body as a string.
    std::string api_get(const std::string &url);
    // Perform a synchronous POST with body/content-type; returns body or "".
    std::string api_post(const std::string &url, const std::string &body, const std::string &content_type);
    // Perform a synchronous PUT of raw bytes; returns body or "".
    std::string api_put(const std::string &url, const uint8_t *data, size_t len, const std::string &content_type);
    // Perform a synchronous PATCH with body/content-type; returns body or "".
    std::string api_patch(const std::string &url, const std::string &body, const std::string &content_type);
    // Perform a synchronous DELETE. Returns true on success (200/204).
    bool api_delete(const std::string &url);

    // Percent-encode a path, leaving '/' separators intact.
    static std::string url_encode_path(const std::string &s);

    // Extract a string field from a cJSON object; returns "" if absent.
    static std::string json_str(cJSON *obj, const char *key);

    // Maximum bytes to buffer for a single write operation.
    static constexpr size_t WRITE_BUF_LIMIT = 65536;
};

#endif /* NETWORKPROTOCOLONEDRIVE_H */
