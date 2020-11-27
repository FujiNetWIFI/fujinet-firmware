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
     * @brief Open a file via URL.
     * @param url pointer to EdUrlParser pointing to file to open.
     * @param cmdFrame pointer to command frame to grab aux1/aux2 values.
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_file(EdUrlParser *url, cmdFrame_t *cmdFrame);

    /**
     * @brief Open a Directory via URL.
     * @param url pointer to EdUrlParser pointing to file to open.
     * @param cmdFrame pointer to command frame to grab aux1/aux2 values.
     * @return FALSE if successful, TRUE on error.
     */
    virtual bool open_dir(EdUrlParser *url, cmdFrame_t *cmdFrame);

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

private:

    /**
     * TNFS MountInfo structure
     */
    tnfsMountInfo mountInfo;

    /**
     * Last TNFS error
     */
    int tnfs_error;

};

#endif /* NETWORKPROTOCOLTNFS_H */