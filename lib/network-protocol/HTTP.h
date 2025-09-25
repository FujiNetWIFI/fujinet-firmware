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
    virtual uint8_t special_inquiry(uint8_t cmd);

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool special_00(cmdFrame_t *cmdFrame);

protected:
    /**
     * @brief open a file handle to fd
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_file_handle();

    /**
     * @brief Open directory handle
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_dir_handle();

    /**
     * @brief Do mount
     * @param url the url to mount
     * @return false on no error, true on error.
     */
    virtual bool mount(PeoplesUrlParser *url);

    /**
     * @brief Unmount TNFS server specified in mountInfo.
     * @return  false on no error, true on error.
     */
    virtual bool umount();

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    virtual void fserror_to_error();

    /**
     * @brief Read from file handle
     * @param buf destination buffer
     * @param len the number of bytes requested
     * @return FALSE if success, TRUE if error
     */
    virtual bool read_file_handle(uint8_t *buf, unsigned short len);

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    virtual bool read_dir_entry(char *buf, unsigned short len);

    /**
     * @brief close file handle
     * @return FALSE if success, true if error
     */
    virtual bool close_file_handle();

    /**
     * @brief Close directory handle
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool close_dir_handle();

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return TRUE on error, FALSE on success.
     */
    virtual bool write_file_handle(uint8_t *buf, unsigned short len);

    /**
     * @brief return status from channel
     * @param Pointer to NetworkStatus object to inject new data.
     * @return FALSE if success, TRUE if error.
     */
    virtual bool status_file(NetworkStatus *status);

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    virtual bool stat();

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    virtual bool rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    virtual bool del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    virtual bool mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return TRUE on error, FALSE on success
     */
    virtual bool rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

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

    /**
     * @brief Set Channel mode (DATA, HEADERS, etc.)
     * @param cmdFrame the passed in command frame.
     */
    bool special_set_channel_mode(cmdFrame_t *cmdFrame);

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

// moved to WebDAV.cpp
//
// /**
//      * @brief Template to wrap Start call.
//      * @param data pointer to parent class
//      * @param El the current element being parsed
//      * @param attr the array of attributes attached to element
//      */
// template <class T>
// void Start(void *data, const XML_Char *El, const XML_Char **attr)
// {
//     T *handler = static_cast<T *>(data);
//     handler->Start(El, attr);
// }

// /**
//  * @brief Template to wrap End call
//  * @param data pointer to parent class.
//  * @param El the current element being parsed.
//  **/
// template <class T>
// void End(void *data, const XML_Char *El)
// {
//     T *handler = static_cast<T *>(data);
//     handler->End(El);
// }

// /**
//  * @brief template to wrap character data.
//  * @param data pointer to parent class
//  * @param s pointer to the character data
//  * @param len length of character data at pointer
//  **/
// template <class T>
// void Char(void *data, const XML_Char *s, int len)
// {
//     T *handler = static_cast<T *>(data);
//     handler->Char(s, len);
// }

#endif /* NETWORKPROTOCOLHTTP_H */
