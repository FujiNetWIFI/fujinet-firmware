/**
 * Network Protocol implementation for TCP sockets
 */

#ifndef NETWORKPROTOCOL_TCP
#define NETWORKPROTOCOL_TCP

#include "Protocol.h"

#include "fnTcpClient.h"
#include "fnTcpServer.h"

class NetworkProtocolTCP : public NetworkProtocol
{
public:
    /**
     * ctor
     */
    NetworkProtocolTCP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolTCP();

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @param mode The open mode to use
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open(PeoplesUrlParser *urlParser, netProtoOpenMode_t omode,
                       netProtoTranslation_t translate) override;

    /**
     * @brief Close connection to the protocol.
     */
    netProtoErr_t close() override;

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
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
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_00(fujiCommandID_t cmd, uint8_t httpChanMode) override;

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_40(uint8_t *sp_buf, unsigned short len, fujiCommandID_t cmd) override { return NETPROTO_ERR_NONE; }

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    netProtoErr_t special_80(uint8_t *sp_buf, unsigned short len, fujiCommandID_t cmd) override { return NETPROTO_ERR_NONE; }

protected:
    /**
     * a fnTcpServer object representing a listening TCP server socket.
     */
    fnTcpServer *server;

    /**
     * a fnTcpClient object representing a client TCP socket.
     */
    fnTcpClient client;


    /**
     * Open a server (listening) connection.
     * @param port bind to port #
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_server(unsigned short port);

    /**
     * Open a client connection to host and port.
     * @param hostname The hostname to connect to.
     * @param port the port number to connect to.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open_client(std::string hostname, unsigned short port);

    /**
     * Special: Accept a server connection, transfer to client socket.
     */
    netProtoErr_t special_accept_connection();

    /**
     * Special: Close client connection.
     */
    netProtoErr_t special_close_client_connection();

    /**
     * Return status of client connection
     * @param status pointer to destination NetworkStatus object
     */
    void status_server(NetworkStatus* status);

    /**
     * Return status of server connection
     * @param status pointer to destination NetworkStatus object
     */
    void status_client(NetworkStatus* status);

    size_t available() override;
};

#endif /* NETWORKPROTOCOL_TCP */
