/**
 * Base interface for Protocol adapters that deal with filesystems
 */

#ifndef NETWORKPROTOCOL_FS
#define NETWORKPROTOCOL_FS

#include "Protocol.h"

class NetworkProtocolFS : public NetworkProtocol
{
    /**
     * @brief Open a URL
     * @param url pointer to EdUrlParser pointing to file to open.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return error flag TRUE on error, FALSE on success.
     */
    virtual bool open(EdUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Close the open URL
     * @return error flag TRUE on error, FALSE on success.
     */
    virtual bool close();

    /**
     * @brief Read len bytes from the open URL.
     * @param len Length in bytes.
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool read(unsigned short len);

    /**
     * @brief Write len bytes to the open URL.
     * @param len Length in bytes.
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool write(unsigned short len);

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool status(NetworkStatus *status);

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

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    virtual bool special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame);

    /**
     * @brief Delete a file specified by URL.
     * @param url pointer to EdUrlParser pointing to file to delete
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool del(EdUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Rename a file specified by URL.
     * @param url pointer to EdUrlParser pointing to file to rename, path contains a comma seperated form of oldname,newname
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool rename(EdUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Make a directory at specified URL.
     * @param url pointer to EdUrlParser pointing to a directory to create.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Remove directory at specified URL.
     * @param url pointer to EdUrlParser pointing to a directory to remove.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame);
    
};

#endif /* NETWORKPROTOCOL_FS */