#ifndef NETWORKPROTOCOLSD_H
#define NETWORKPROTOCOLSD_H

#include <cstdio>
#include "FS.h"


class NetworkProtocolSD : public NetworkProtocolFS
{
public:
    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolSD(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolSD();

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    AtariSIODirection special_inquiry(fujiCommandID_t cmd) override;

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t special_00(cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    protocolError_t special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

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
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t open_file_handle() override;

    /**
     * @brief Open directory handle
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t open_dir_handle() override;

    /**
     * @brief Do FS mount: SD should be always mounted, SD is only checked here
     * @param url The URL to mount
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t mount(PeoplesUrlParser *url) override;

    /**
     * @brief Unmount FS: SD should stay mounted, nothing is done here
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
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
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
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
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t write_file_handle(uint8_t *buf, unsigned short len) override;

    /**
     * @brief close file handle
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t close_file_handle() override;

    /**
     * @brief Close directory handle
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t close_dir_handle() override;

    off_t seek(off_t offset, int whence) override;

private:

    /**
     * The create permissions of the open file
     */
    uint16_t perms = 0;

    /**
     * The resulting file handle of open file.
     */
    ::FILE *fh = nullptr;

    /**
     * @brief Check if SD file system is running
     * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
     */
    protocolError_t check_fs();

    /**
     * @brief Translate stdlib's errno to protocol error code.
     */
    void errno_to_error() override;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    protocolError_t stat() override;
};

#endif /* NETWORKPROTOCOLSD_H */
