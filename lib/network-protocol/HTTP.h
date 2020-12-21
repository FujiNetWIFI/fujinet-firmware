#ifndef NETWORKPROTOCOLHTTP_H
#define NETWORKPROTOCOLHTTP_H

#include "FS.h"
#include "../http/fnHttpClient.h"

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
    NetworkProtocolHTTP(string *rx_buf, string *tx_buf, string *sp_buf);

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
    virtual bool mount(EdUrlParser *url);

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
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    virtual bool stat();

private:

    /**
     * The HTTP Open Mode, ultimately used in http_transaction()
     */
    enum _httpOpenMode
    {
        GET,
        POST,
        PUT
    } httpOpenMode;

    /**
     * The HTTP channel mode, used to distinguish between headers and data
     */
    enum _httpChannelMode
    {
        DATA,
        HEADERS
    } httpChannelMode;

    /**
     * The fnHTTPClient object used by the adaptor for HTTP calls
     */
    fnHttpClient *client;

    /**
     * result code returned by an HTTP verb
     */
    int resultCode;

    /**
     * Do HTTP transaction
     */
    void http_transaction();

    /**
     * @brief Set Channel mode (DATA, HEADERS, etc.)
     * @param cmdFrame the passed in command frame.
     */
    bool special_set_channel_mode(cmdFrame_t *cmdFrame);

};

#endif /* NETWORKPROTOCOLHTTP_H */