#ifndef NETWORKPROTOCOLTNFS_H
#define NETWORKPROTOCOLTNFS_H

#include "FS.h"
#include "tnfslib.h"
//#include "tnfslibMountInfo.h"

class NetworkProtocolTNFS : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolTNFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolTNFS();

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
    netProtoErr_t special_00(fujiCommandID_t cmd, uint8_t httpChanMode) override { return NetworkProtocolFS::special_00(cmd, httpChanMode); }

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    netProtoErr_t special_80(uint8_t *sp_buf, unsigned short len, fujiCommandID_t cmd) override { return NETPROTO_ERR_NONE; }

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

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t lock(PeoplesUrlParser *url) override;

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t unlock(PeoplesUrlParser *url) override;

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
     * @brief Do TNFS mount
     * @param url The URL to mount
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
     *        tnfs_write() blocks.
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
     * TNFS MountInfo structure
     */
    tnfsMountInfo mountInfo;

    /**
     * Last TNFS error
     */
    int tnfs_error = 0;

    /**
     * The create permissions of the open file
     */
    uint16_t perms = 0;

    /**
     * The resulting file handle of open file.
     */
    int16_t fd = 0;

    /**
     * The TNFS filestat of the currently open file.
     */
    tnfsStat fileStat;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    netProtoErr_t stat() override;
};

#endif /* NETWORKPROTOCOLTNFS_H */
