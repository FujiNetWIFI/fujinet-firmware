/**
 * Base interface for Protocol adapters that deal with filesystems
 */

#ifndef NETWORKPROTOCOL_FS
#define NETWORKPROTOCOL_FS

#include "Protocol.h"

class NetworkProtocolFS : public NetworkProtocol
{
    /**
     * @brief Delete a file specified by URL.
     * @param url pointer to EdUrlParser pointing to file to delete
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool del(EdUrlParser *url, cmdFrame_t *cmdFrame) = 0;

    /**
     * @brief Rename a file specified by URL.
     * @param url pointer to EdUrlParser pointing to file to rename, path contains a comma seperated form of oldname,newname
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values
     * @return error flag TRUE on error, FALSE on success
     */
    virtual bool rename(EdUrlParser *url, cmdFrame_t *cmdFrame) = 0;

    /**
     * @brief Make a directory at specified URL.
     * @param url pointer to EdUrlParser pointing to a directory to create.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame) = 0;

    /**
     * @brief Remove directory at specified URL.
     * @param url pointer to EdUrlParser pointing to a directory to remove.
     * @param cmdFrame pointer to command frame for aux1/aux2/etc values.
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame) = 0;
    
};

#endif /* NETWORKPROTOCOL_FS */