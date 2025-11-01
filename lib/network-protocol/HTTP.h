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
    FujiDirection special_inquiry(uint8_t cmd) override;

#ifdef OBSOLETE
    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool special_00(cmdFrame_t *cmdFrame);
#endif /* OBSOLETE */

protected:
    /**
     * @brief open a file handle to fd
     * @return FALSE if successful, TRUE on error.
     */
    bool open_file_handle() override;

    /**
     * @brief Open directory handle
     * @return FALSE if successful, TRUE on error.
     */
    bool open_dir_handle() override;

    /**
     * @brief Do mount
     * @param url the url to mount
     * @return false on no error, true on error.
     */
    bool mount(PeoplesUrlParser *url) override;

    /**
     * @brief Unmount TNFS server specified in mountInfo.
     * @return  false on no error, true on error.
     */
    bool umount() override;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    void fserror_to_error() override;

    /**
     * @brief Read from file handle
     * @param buf destination buffer
     * @param len the number of bytes requested
     * @return FALSE if success, TRUE if error
     */
    bool read_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    bool read_dir_entry(char *buf, unsigned short len) override;

    /**
     * @brief close file handle
     * @return FALSE if success, true if error
     */
    bool close_file_handle() override;

    /**
     * @brief Close directory handle
     * @return FALSE if successful, TRUE on error.
     */
    bool close_dir_handle() override;

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return TRUE on error, FALSE on success.
     */
    bool write_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief return status from channel
     * @param Pointer to NetworkStatus object to inject new data.
     * @return FALSE if success, TRUE if error.
     */
    bool status_file(NetworkStatus *status) override ;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    bool stat() override;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    bool rename(PeoplesUrlParser *url) override;

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    bool del(PeoplesUrlParser *url) override;

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    bool mkdir(PeoplesUrlParser *url) override;

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    bool rmdir(PeoplesUrlParser *url) override;

private:
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

#ifdef OBSOLETE
    /**
     * @brief Set Channel mode (DATA, HEADERS, etc.)
     * @param cmdFrame the passed in command frame.
     */
    bool special_set_channel_mode(cmdFrame_t *cmdFrame);
#endif /* OBSOLETE */

    /**
     * @brief header mode - retrieve requested headers previously collected.
     * @param buf The target buffer
     * @param len The target buffer length
     * @return true on ERROR FALSE on success
     */
    bool read_file_handle_header(uint8_t *buf, unsigned short len);

    /**
     * @brief data mode - read
     * @param buf The target buffer
     * @param len The target buffer length
     * @return true on ERROR FALSE on success
     */
    bool read_file_handle_data(uint8_t *buf, unsigned short len);

    /**
     * @brief header mode - write requested headers to pass into collect_headers.
     * @param buf The source buffer
     * @param len The source buffer length
     * @return true on ERROR FALSE on success
     */
    bool write_file_handle_get_header(uint8_t *buf, unsigned short len);

    /**
     * @brief header mode - write specified header to server
     * @param buf The source buffer
     * @param len The source buffer length
     * @return true on ERROR FALSE on success
     */
    bool write_file_handle_set_header(uint8_t *buf, unsigned short len);

    /**
     * @brief post mode - write specified post data to server
     * @param buf The source buffer
     * @param len The source buffer length
     * @return true on ERROR FALSE on success
     */
    bool write_file_handle_send_post_data(uint8_t *buf, unsigned short len);

    /**
     * @brief data mode - write requested headers to pass into PUT
     * @param buf The source buffer
     * @param len The source buffer length
     * @return true on ERROR FALSE on success
     */
    bool write_file_handle_data(uint8_t *buf, unsigned short len);

    /**
     * @brief Parse directory retrieved from PROPFIND
     * @param buf the source buffer
     * @param len the buffer length
     * @return TRUE on error, FALSE on success.
     */
    bool parseDir(char *buf, unsigned short len);
};

#endif /* NETWORKPROTOCOLHTTP_H */
