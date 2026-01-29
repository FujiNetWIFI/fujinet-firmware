#ifndef NETWORKPROTOCOLHTTP_H
#define NETWORKPROTOCOLHTTP_H

#include <expat.h>

#include "WebDAV.h"
#include "FS.h"

#ifdef ESP_PLATFORM
#include "fnHttpClient.h"
#define HTTP_CLIENT_CLASS fnHttpClient
#else
#include "mgHttpClient.h"
#define HTTP_CLIENT_CLASS mgHttpClient
#endif

// on Windows/MinGW DELETE is defined already ...
#if defined(_WIN32) && defined(DELETE)
#undef DELETE
#endif

enum netProtoHTTPChannelMode_t {
    HTTP_CHANMODE_BODY            = 0,
    HTTP_CHANMODE_COLLECT_HEADERS = 1,
    HTTP_CHANMODE_GET_HEADERS     = 2,
    HTTP_CHANMODE_SET_HEADERS     = 3,
    HTTP_CHANMODE_SET_POST_DATA   = 4,
};

#define OPEN_MODE_HTTP_GET      (0x04)
#define OPEN_MODE_HTTP_PUT      (0x08)
#define OPEN_MODE_HTTP_GET_H    (0x0C)
#define OPEN_MODE_HTTP_POST     (0x0D)
#define OPEN_MODE_HTTP_PUT_H    (0x0E)
#define OPEN_MODE_HTTP_DELETE   (0x05)
#define OPEN_MODE_HTTP_DELETE_H (0x09)

class NetworkProtocolHTTP : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolHTTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolHTTP();

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    AtariSIODirection special_inquiry(fujiCommandID_t cmd) override;

    /**
     * @brief execute a command that returns no payload
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_00(fujiCommandID_t cmd, uint8_t httpChanMode) override;

protected:
    /**
     * @brief open a file handle to fd
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_file_handle(netProtoOpenMode_t omode) override;

    /**
     * @brief Open directory handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_dir_handle() override;

    /**
     * @brief Do mount
     * @param url the url to mount
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t mount(PeoplesUrlParser *url) override;

    /**
     * @brief Unmount TNFS server specified in mountInfo.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t umount() override;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    void fserror_to_error() override;

    /**
     * @brief Read from file handle
     * @param buf destination buffer
     * @param len the number of bytes requested
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    netProtoErr_t read_dir_entry(char *buf, unsigned short len) override;

    /**
     * @brief close file handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t close_file_handle() override;

    /**
     * @brief Close directory handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t close_dir_handle() override;

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief return status from channel
     * @param Pointer to NetworkStatus object to inject new data.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t status_file(NetworkStatus *status) override;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    netProtoErr_t stat() override;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t rename(PeoplesUrlParser *url) override;

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t del(PeoplesUrlParser *url) override;

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t mkdir(PeoplesUrlParser *url) override;

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t rmdir(PeoplesUrlParser *url) override;

private:
    netProtoOpenMode_t _httpStreamMode;

    /**
     * The HTTP Open Mode, ultimately used in http_transaction()
     */
    typedef enum _httpOpenMode
    {
        GET,
        POST,
        PUT,
        DELETE
    } HTTPOpenMode;

    HTTPOpenMode httpOpenMode = HTTPOpenMode::GET;

    /**
     * The HTTP channel mode, used to distinguish between headers and data
     */
    typedef enum _httpChannelMode
    {
        DATA,
        COLLECT_HEADERS,
        GET_HEADERS,
        SET_HEADERS,
        SEND_POST_DATA
    } HTTPChannelMode;

    HTTPChannelMode httpChannelMode;

    /**
     * The fnHTTPClient object used by the adaptor for HTTP calls
     */
    HTTP_CLIENT_CLASS *client = nullptr;

    /**
     * result code returned by an HTTP verb
     */
    int resultCode = 0;

    /**
     * Headers the client wants us to collect information on if they are seen.
     * The contract is to only collect headers the client is interested in, which they register for before making the request.
     */
    // char *collect_headers[32];
    std::vector<std::string> collect_headers;

    /**
     * Collected headers count
     */
    // size_t collect_headers_count = 0;

    /**
     * Returned headers
     */
    std::vector<std::string> returned_headers;

    /**
     * Returned header cursor
     */
    size_t returned_header_cursor = 0;

    /**
     * Body size (fileSize is reset with this when DATA is engaged)
     */
    int bodySize = 0;

    /**
     * POST or PUT Data to send.
     */
    std::string postData;

    /**
     * WebDAV handler
     */
    WebDAV webDAV;

    /**
     * Current Directory entry cursor
     */
    std::vector<WebDAV::DAVEntry>::iterator dirEntryCursor;

    /**
     * Do HTTP transaction
     */
    void http_transaction();

    /**
     * @brief Set Channel mode (DATA, HEADERS, etc.)
     */
    netProtoErr_t special_set_channel_mode(netProtoHTTPChannelMode_t newMode);

    /**
     * @brief header mode - retrieve requested headers previously collected.
     * @param buf The target buffer
     * @param len The target buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read_file_handle_header(uint8_t *buf, unsigned short len);

    /**
     * @brief data mode - read
     * @param buf The target buffer
     * @param len The target buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read_file_handle_data(uint8_t *buf, unsigned short len);

    /**
     * @brief header mode - write requested headers to pass into collect_headers.
     * @param buf The source buffer
     * @param len The source buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle_get_header(uint8_t *buf, unsigned short len);

    /**
     * @brief header mode - write specified header to server
     * @param buf The source buffer
     * @param len The source buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle_set_header(uint8_t *buf, unsigned short len);

    /**
     * @brief post mode - write specified post data to server
     * @param buf The source buffer
     * @param len The source buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle_send_post_data(uint8_t *buf, unsigned short len);

    /**
     * @brief data mode - write requested headers to pass into PUT
     * @param buf The source buffer
     * @param len The source buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle_data(uint8_t *buf, unsigned short len);

    /**
     * @brief Parse directory retrieved from PROPFIND
     * @param buf the source buffer
     * @param len the buffer length
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t parseDir(char *buf, unsigned short len);

    size_t available() override;
};

#endif /* NETWORKPROTOCOLHTTP_H */
