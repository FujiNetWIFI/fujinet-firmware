#ifndef NETWORKPROTOCOLTNFS_H
#define NETWORKPROTOCOLTNFS_H

#include "FS.h"
#include "tnfslib.h"
#include "tnfslibMountInfo.h"

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
    NetworkProtocolTNFS(string *rx_buf, string *tx_buf, string *sp_buf);

    /**
     * dTOR
     */
    virtual ~NetworkProtocolTNFS();

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

protected:
    /**
     * @brief Open a file at path.
     * @param path the path to open.
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_file(string path);

    /**
     * @brief Open a Directory via URL.
     * @param path the path to open
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_dir(string path);

    /**
     * @brief Do TNFS mount
     * @param hostName - host name of TNFS server
     * @param path - path to mount, usually "/"
     * @return false on no error, true on error.
     */
    virtual bool mount(string hostName, string path);

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
     * @brief Resolve filename at path. Gets directory, searches for file,
     *        if path not found, the file is passed through util_crunch,
     *        and a second attempt is done.
     * @param path The full path to file to resolve.
     * @return resolved path.
     */
    virtual string resolve(string path);

    /**
     * @brief Read from file
     * @param len the number of bytes requested
     * @return FALSE if success, TRUE if error.
     */
    virtual bool read_file(unsigned short len);

    /**
     * @brief Read from directory
     * @param len the number of bytes requested
     * @return FALSE if success, TRUE if error
     */
    virtual bool read_dir(unsigned short len);

    /**
     * @brief return status from file (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return FALSE if success, TRUE if error.
     */
    virtual bool status_file(NetworkStatus *status);

    /**
     * @brief return status from directory (e.g. # of bytes remaining.)
     * @param Pointer to NetworkStatus object to inject new data.
     * @return FALSE if success, TRUE if error.
     */
    virtual bool status_dir(NetworkStatus *status);

    /**
     * @brief close file.
     * @return FALSE if success, true if error.
     */
    virtual bool close_file();

    /**
     * @brief close directory.
     * @return FALSE if success, true if error.
     */
    virtual bool close_dir();

    /**
     * @brief Write to file
     * @param len the number of bytes requested
     * @return FALSE if successful, TRUE if error.
     */
    virtual bool write_file(unsigned short len);

    /**
     * @brief Rename file specified by incoming devicespec.
     * @param sp_buf Pointer to special buffer
     * @param len of special buffer.
     * @return TRUE on error, FALSE on success
     */
    bool rename(uint8_t* sp_buf, unsigned short len);

private:
    /**
     * TNFS MountInfo structure
     */
    tnfsMountInfo mountInfo;

    /**
     * Last TNFS error
     */
    int tnfs_error;

    /**
     * The mode of the open file
     */
    uint16_t mode;

    /**
     * The create permissions of the open file
     */
    uint16_t perms;

    /**
     * The resulting file handle of open file.
     */
    int16_t fd;

    /**
     * The TNFS filestat of the currently open file.
     */
    tnfsStat fileStat;

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_read() blocks.
     * @param buf buffer to transfer into.
     * @param len Requested # of bytes.
     * @return TRUE on error, FALSE on success.
     */
    bool block_read(uint8_t *buf, unsigned short len);

    /**
     * @brief for len requested, break up into number of required
     *        tnfs_write() blocks.
     * @param len Requested # of bytes.
     * @return TRUE on error, FALSE on success.
     */
    bool block_write(uint8_t *buf, unsigned short len);

    /**
     * @brief Delete file specified by incoming devicespec.
     * @param sp_buf pointer to special buffer
     * @param len of special buffer
     * @return TRUE on error, FALSE on success
     */
    bool del(uint8_t* sp_buf, unsigned short len);

    /**
     * @brief Make directory specified by incoming devicespec.
     * @param sp_buf pointer to special buffer
     * @param len of special buffer
     * @return TRUE on error, FALSE on success
     */
    bool mkdir(uint8_t* sp_buf, unsigned short len);

    /**
     * @brief Remove directory specified by incoming devicespec.
     * @param sp_buf pointer to special buffer
     * @param len of special buffer
     * @return TRUE on error, FALSE on success
     */
    bool rmdir(uint8_t* sp_buf, unsigned short len);
};

#endif /* NETWORKPROTOCOLTNFS_H */