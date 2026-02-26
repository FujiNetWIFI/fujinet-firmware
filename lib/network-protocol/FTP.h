#ifndef NETWORKPROTOCOLFTP_H
#define NETWORKPROTOCOLFTP_H

#include "FS.h"

#include "fnFTP.h"

class NetworkProtocolFTP : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolFTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolFTP();

    /**
     *  Class 'NetworkProtocolFTP' does not have a copy constructor which is recommended since it has dynamic memory/resource allocation(s).
     * Unless these two functions are implemented, they are being deleted so they cannot be used
     */
    NetworkProtocolFTP (const NetworkProtocolFTP&) = delete;
    NetworkProtocolFTP& operator= (const NetworkProtocolFTP&) = delete;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t rename(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t del(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t mkdir(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t rmdir(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t lock(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t unlock(PeoplesUrlParser *url) override { return PROTOCOL_ERROR::NONE; }

    size_t available() override;

protected:
    /**
     * Is rename implemented?
     */
    bool rename_implemented = true;

    /**
     * Is delete implemented?
     */
    bool delete_implemented = true;

    /**
     * Is mkdir implemented?
     */
    bool mkdir_implemented = true;

    /**
     * Is rmdir implemented?
     */
    bool rmdir_implemented = true;

    /**
     * @brief Open file handle, set fd
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open_file_handle() override;

    /**
     * @brief Open directory handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open_dir_handle() override;

    /**
     * @brief Do FTP mount
     * @param url the URL to mount
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t mount(PeoplesUrlParser *url) override;

    /**
     * @brief Unmount FTP server specified in mountInfo.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t umount() override;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    void fserror_to_error() override;

    /**
     * @brief Read from file handle
     * @param buf target buffer
     * @param len the number of bytes requested
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t read_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    protocolError_t read_dir_entry(char *buf, unsigned short len) override;

    /**
     * @brief for len requested, break up into number of required
     *        FTP_write() blocks.
     * @param len Requested # of bytes.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t write_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief close file handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t close_file_handle() override;

    /**
     * @brief Close directory handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t close_dir_handle() override;

    /**
     * @brief return status from file (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t status_file(NetworkStatus *status) override;

private:
    /**
     * fnFTP instance
     */
    fnFTP *ftp;

    /**
     * TRUE = STOR, FALSE = RETR
     */
    bool stor = false;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    protocolError_t stat() override { return PROTOCOL_ERROR::NONE; }
};

#endif /* NETWORKPROTOCOLFTP_H */
