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
    enum _openMode
    {
        GET,
        POST,
        PUT
    } openMode;

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

};

#endif /* NETWORKPROTOCOLHTTP_H */