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
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close connection to the protocol.
     */
    protocolError_t close() override;

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
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
     * Accept a server connection, transfer to client socket.
     */
    protocolError_t accept_connection();

    /**
     * Close client connection.
     */
    protocolError_t close_client_connection();

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
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open_server(unsigned short port);

    /**
     * Open a client connection to host and port.
     * @param hostname The hostname to connect to.
     * @param port the port number to connect to.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open_client(std::string hostname, unsigned short port);

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
