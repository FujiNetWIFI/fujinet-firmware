#ifndef NETWORKPROTOCOLSFTP_H
#define NETWORKPROTOCOLSFTP_H

#include "FS.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

class NetworkProtocolSFTP : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolSFTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolSFTP();

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t rename(PeoplesUrlParser *url) override;

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t del(PeoplesUrlParser *url) override;

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t mkdir(PeoplesUrlParser *url) override;

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t rmdir(PeoplesUrlParser *url) override;

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t lock(PeoplesUrlParser *url) override;

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t unlock(PeoplesUrlParser *url) override;

    off_t seek(off_t offset, int whence) override;

protected:

    /**
     * @brief Open file handle, set fd
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open_file_handle() override;

    /**
     * @brief Open directory handle
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open_dir_handle() override;

    /**
     * @brief Do SFTP mount (SSH connect + authenticate + sftp_init)
     * @param url The URL to mount
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t mount(PeoplesUrlParser *url) override;

    /**
     * @brief Tear down SFTP subsystem and SSH session.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t umount() override;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    void fserror_to_error() override;

    /**
     * @brief Read from file handle
     * @param buf target buffer
     * @param len the number of bytes requested
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t read_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    fujiError_t read_dir_entry(char *buf, unsigned short len) override;

    /**
     * @brief for len requested, break up into number of required
     *        sftp_write() blocks.
     * @param len Requested # of bytes.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t write_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief close file handle
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t close_file_handle() override;

    /**
     * @brief Close directory handle
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t close_dir_handle() override;

private:
    /**
     * The libssh session
     */
    ssh_session session = nullptr;

    /**
     * The libssh SFTP subsystem session
     */
    sftp_session sftp = nullptr;

    /**
     * The resulting file handle of open file.
     */
    sftp_file fh = nullptr;

    /**
     * The open directory handle.
     */
    sftp_dir dir_handle = nullptr;

    /**
     * Last SFTP error (SSH_FX_*)
     */
    int sftp_err = SSH_FX_OK;

    /**
     * Offset through file
     */
    uint64_t offset = 0;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    fujiError_t stat() override;

    /**
     * @brief Connect and authenticate the SSH session from url.
     * @return true on success
     */
    bool sshConnectAndAuth(PeoplesUrlParser *url);

    /**
     * @brief Authenticate using password from URL.
     * @return true on success
     */
    bool authenticateWithPassword();

    /**
     * @brief Authenticate using default private key from SD card.
     * @return true on success
     */
    bool authenticateWithDefaultKey();

    /**
     * @brief Get the filesystem path to the default SSH private key on SD card.
     * @return absolute path to /.ssh/id_ed25519 on the SD card
     */
    std::string getDefaultPrivateKeyPath();
};

#endif /* NETWORKPROTOCOLSFTP_H */
