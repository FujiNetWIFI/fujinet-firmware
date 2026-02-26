/**
 * Base interface for Protocol adapters that deal with filesystems
 */

#ifndef NETWORKPROTOCOL_FS
#define NETWORKPROTOCOL_FS

#include "Protocol.h"

typedef enum class APPLE2_FLAG {
    IS_A2    = 0x80,
    IS_80COL = 0x81,
} apple2Flag_t;

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
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close the open URL
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t close() override;

    /**
     * @brief Read len bytes from the open URL.
     * @param len Length in bytes.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes to the open URL.
     * @param len Length in bytes.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t status(NetworkStatus *status) override;

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file/dest to rename
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t rename(PeoplesUrlParser *url);

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t del(PeoplesUrlParser *url) { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t mkdir(PeoplesUrlParser *url) { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t rmdir(PeoplesUrlParser *url) { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief lock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t lock(PeoplesUrlParser *url) { return PROTOCOL_ERROR::NONE; }

    /**
     * @brief unlock file specified by incoming devicespec.
     * @param url pointer to PeoplesUrlParser pointing to file to delete
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t unlock(PeoplesUrlParser *url) { return PROTOCOL_ERROR::NONE; }

    size_t available() override;

protected:
    /**
     * stream mode flag from open
     */
    fileAccessMode_t streamMode = ACCESS_MODE::INVALID;

    /**
     * Open mode typedef
     */
    typedef enum _streamType
    {
        FILE,
        DIR
    } streamType_t;

    /**
     * Open mode
     */
    streamType_t streamType = streamType_t::FILE;

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
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open_file();

    /**
     * @brief open a file handle to fd
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open_file_handle() = 0;

    /**
     * @brief Open a Directory via path
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open_dir(apple2Flag_t a2flags);

    /**
     * @brief Open directory handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t open_dir_handle() = 0;

    /**
     * @brief Do mount
     * @param url the url to mount
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t mount(PeoplesUrlParser *url) = 0;

    /**
     * @brief Unmount TNFS server specified in mountInfo.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t umount() = 0;

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
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t read_file(unsigned short len);

    /**
     * @brief Read from file handle
     * @param buf destination buffer
     * @param len the number of bytes requested
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t read_file_handle(uint8_t *buf, unsigned short len) = 0;

    /**
     * @brief Read from directory
     * @param len the number of bytes requested
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t read_dir(unsigned short len);

    /**
     * @brief read next directory entry.
     * @param buf the target buffer
     * @param len length of target buffer
     */
    virtual protocolError_t read_dir_entry(char *buf, unsigned short len) = 0;

    /**
     * @brief return status from file (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t status_file(NetworkStatus *status);

    /**
     * @brief return status from directory (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t status_dir(NetworkStatus *status);

    /**
     * @brief close file.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t close_file();

    /**
     * @brief close file handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t close_file_handle() = 0;

    /**
     * @brief close directory.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t close_dir();

    /**
     * @brief Close directory handle
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t close_dir_handle() = 0;

    /**
     * @brief Write to file
     * @param len the number of bytes requested
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t write_file(unsigned short len);

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    virtual protocolError_t write_file_handle(uint8_t *buf, unsigned short len) = 0;

    /**
     * @brief get status of file, filling in filesize. mount() must have already been called.
     */
    virtual protocolError_t stat() = 0;

    /**
     * @brief change the values passed to open for platforms that need to do it after the open (looking a you IEC)
     */
    void set_open_params(fileAccessMode_t access, netProtoTranslation_t translate) override;
};

#endif /* NETWORKPROTOCOL_FS */
