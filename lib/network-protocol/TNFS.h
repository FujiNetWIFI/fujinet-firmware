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
};

#endif /* NETWORKPROTOCOLTNFS_H */