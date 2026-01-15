/**
 * Base interface for Protocol adapters that deal with filesystems
 */

#ifndef NETWORKPROTOCOL_FS
#define NETWORKPROTOCOL_FS

#include "Protocol.h"

class NetworkProtocolFS : public NetworkProtocol
{
public:
    /**
     * Is rename implemented?
     */
    bool rename_implemented = false;

    /**
     * Is delete implemented?
     */
    bool delete_implemented = false;

    /**
     * Is mkdir implemented?
     */
    bool mkdir_implemented = false;

    /**
     * Is rmdir implemented?
     */
    bool rmdir_implemented = false;

    /**
     * @brief ctor
     * @param rx_buf pointer to receive buffer
     * @param tx_buf pointer to transmit buffer
     * @param sp_buf pointer to special buffer
     * @return a NetworkProtocolFS object
     */
    NetworkProtocolFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolFS();

    /**
     * @brief Open a URL
     * @param url pointer to PeoplesUrlParser pointing to file to open.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Close the open URL
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t close() override;

    /**
     * @brief Read len bytes from the open URL.
     * @param len Length in bytes.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read(unsigned short len) override;

    /**
     * @brief Write len bytes to the open URL.
     * @param len Length in bytes.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t status(NetworkStatus *status) override;

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    AtariSIODirection special_inquiry(fujiCommandID_t cmd) override;

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_00(cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    netProtoErr_t special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    /**
     * @brief perform an idempotent command with DSTATS 0x80, that does not require open channel.
     * @param url The URL object.
     * @param cmdFrame command frame.
     */
    netProtoErr_t perform_idempotent_80(PeoplesUrlParser *url, cmdFrame_t *cmdFrame) override;

    size_t available() override;

protected:
    /**
     * Open mode typedef
     */
    typedef enum _openMode
    {
        FILE,
        DIR
    } OpenMode;

    /**
     * Open mode
     */
    OpenMode openMode = OpenMode::FILE;

    /**
     * Directory of currently open file
     */
    std::string dir;

    /**
     * Filename of currently open file
     */
    std::string filename;

    /**
     * Filename for destination (e.g. rename)
     */
    std::string destFilename;

    /**
     * File size
     */
    int fileSize = 0;

    /**
     * Directory buffer
     */
    std::string dirBuffer;

    /**
     * Is open file a directory?
     */
    bool is_directory = false;

    /**
     * The mode of the open file
     */
    uint16_t mode = 0;

    /**
     * Is open file locked?
     */
    bool is_locked = false;

    /**
     * @brief Open a file via path.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t open_file();

    /**
     * @brief open a file handle to fd
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t open_file_handle() = 0;

    /**
     * @brief Open a Directory via path
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t open_dir();

    /**
     * @brief Open directory handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t open_dir_handle() = 0;

    /**
     * @brief Do mount
     * @param url the url to mount
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t mount(PeoplesUrlParser *url) = 0;

    /**
     * @brief Unmount TNFS server specified in mountInfo.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t umount() = 0;

    /**
     * @brief Translate filesystem error codes to Atari error codes. Sets error in Protocol.
     */
    virtual void fserror_to_error() = 0;

    /**
     * @brief Resolve filename at url. Gets directory, searches for file,
     *        if path not found, the file is passed through util_crunch,
     *        and a second attempt is done.
     */
    virtual void resolve();

    /**
     * Update dir and filename
     * @param url the URL to update dir and filename with.
     */
    void update_dir_filename(PeoplesUrlParser *url);

    /**
     * @brief Read from file
     * @param len the number of bytes requested
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t read_file(unsigned short len);

    /**
     * @brief Read from file handle
     * @param buf destination buffer
     * @param len the number of bytes requested
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t read_file_handle(uint8_t *buf, unsigned short len) = 0;

    /**
     * @brief Read from directory
     * @param len the number of bytes requested
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t read_dir(unsigned short len);

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    virtual netProtoErr_t read_dir_entry(char *buf, unsigned short len) = 0;

    /**
     * @brief return status from file (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t status_file(NetworkStatus *status);

    /**
     * @brief return status from directory (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t status_dir(NetworkStatus *status);

    /**
     * @brief close file.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t close_file();

    /**
     * @brief close file handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t close_file_handle() = 0;

    /**
     * @brief close directory.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t close_dir();

    /**
     * @brief Close directory handle
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t close_dir_handle() = 0;

    /**
     * @brief Write to file
     * @param len the number of bytes requested
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t write_file(unsigned short len);

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t write_file_handle(uint8_t *buf, unsigned short len) = 0;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    virtual netProtoErr_t stat() = 0;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @param cmdFrame the command frame
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    virtual netProtoErr_t unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame);

};

#endif /* NETWORKPROTOCOL_FS */
