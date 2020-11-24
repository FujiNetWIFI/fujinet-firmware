#ifndef NETWORKPROTOCOLTNFS_H
#define NETWORKPROTOCOLTNFS_H

#include "FS.h"

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
};

#endif /* NETWORKPROTOCOLTNFS_H */