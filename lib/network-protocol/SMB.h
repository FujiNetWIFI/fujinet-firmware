#ifndef NETWORKPROTOCOLSMB_H
#define NETWORKPROTOCOLSMB_H

#include "FS.h"


class NetworkProtocolSMB : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolSMB(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolSMB();

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_YEPPERS on error
     */
    netProtoErr_t mkdir(PeoplesUrlParser *url) override;

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_YEPPERS on error
     */
    netProtoErr_t rmdir(PeoplesUrlParser *url) override;

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_YEPPERS on error
     */
    netProtoErr_t lock(PeoplesUrlParser *url) override { return NETPROTO_ERR_NONE; }

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_YEPPERS on error
     */
    netProtoErr_t unlock(PeoplesUrlParser *url) override { return NETPROTO_ERR_NONE; }

    off_t seek(off_t offset, int whence) override;

    size_t available() override { return 0; }

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
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_file_handle(netProtoOpenMode_t omode) override;

    /**
     * @brief Open directory handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_dir_handle() override;

    /**
     * @brief Do SMB mount
     * @param url The URL to mount
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t mount(PeoplesUrlParser *url) override;

    /**
     * @brief Unmount SMB server specified in mountInfo.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t umount() override;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    void fserror_to_error() override;

    /**
     * @brief Read from file handle
     * @param buf target buffer
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
     * @brief for len requested, break up into number of required
     *        SMB_write() blocks.
     * @param len Requested # of bytes.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write_file_handle(uint8_t *buf, unsigned short len) override;

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

private:
    /**
     * SMB2 context
     */
    struct smb2_context *smb = nullptr;

    /**
     * SMB2 URL
     */
    struct smb2_url *smb_url = nullptr;

    /**
     * SMB2 directory handle
     */
    struct smb2dir *smb_dir = nullptr;

    /**
     * SMB2 directory entry
     */
    struct smb2dirent *ent = nullptr;

    /**
     * Last SMB error
     */
    int smb_error = 0;

    /**
     * The resulting file handle of open file.
     */
    struct smb2fh *fh = nullptr;

    /**
     * Offset through file
     */
    uint64_t offset = 0;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    netProtoErr_t stat() override;
};

#endif /* NETWORKPROTOCOLSMB_H */
