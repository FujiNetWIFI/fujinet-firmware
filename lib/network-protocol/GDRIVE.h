#ifndef NETWORKPROTOCOLGDRIVE_H
#define NETWORKPROTOCOLGDRIVE_H

#include "FS.h"

#include <cJSON.h>
#include <string>
#include <vector>

#include "GoogleDriveClient.h"

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
    // Shared Google Drive REST + OAuth2 token helper.
    GoogleDriveClient _gdrive;

    // Resolved Google Drive file/folder IDs for open file/dir
    std::string _file_id;
    std::string _parent_folder_id;

    // Directory listing state
    cJSON *_dir_json = nullptr;
    cJSON *_dir_items = nullptr;
    int _dir_item_idx = 0;
    bool _include_file_id = false; // set when DIR_FORMAT::GDRIVE aux2 flag requested

    // Write buffer – data accumulates here until close_file_handle uploads it
    std::vector<uint8_t> _write_buf;

    // Last Google Drive API error message (for debug)
    std::string _last_error;

    // Persistent HTTP client used for streaming file downloads
    GDRIVE_HTTP_CLIENT _http;

    // Maximum bytes to buffer for a single write operation.
    static constexpr size_t WRITE_BUF_LIMIT = 65536;
};

#endif /* NETWORKPROTOCOLGDRIVE_H */
