/**
 * NetworkProtocolTCP
 *
 * TCP Protocol Adapter Implementation
 */

#include "TCP.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include "compat_inet.h"

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <vector>

/**
 * @brief ctor
 * @param rx_buf pointer to receive buffer
 * @param tx_buf pointer to transmit buffer
 * @param sp_buf pointer to special buffer
 * @return a NetworkProtocolTCP object
 */
NetworkProtocolTCP::NetworkProtocolTCP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTCP::ctor\r\n");
    server = nullptr;
}

/**
 * dtor
 */
NetworkProtocolTCP::~NetworkProtocolTCP()
{
    Debug_printf("NetworkProtocolTCP::dtor\r\n");

    if (server != nullptr)
    {
        delete server;
        server = nullptr;
    }
}

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 */
protocolError_t NetworkProtocolTCP::open(PeoplesUrlParser *urlParser,
                                         fileAccessMode_t access,
                                         netProtoTranslation_t translate)
{
    protocolError_t ret = PROTOCOL_ERROR::UNSPECIFIED; // assume error until proven ok

    Debug_printf("NetworkProtocolTCP::open(%s:%s)\r\n", urlParser->host.c_str(), urlParser->port.c_str());

    if (urlParser->host.empty())
    {
        // Open server on port, otherwise, treat as empty socket.
        if (!urlParser->port.empty())
            ret = open_server(urlParser->getPort());
        else
        {
            Debug_printf("Empty socket enabled.\r\n");
            ret = PROTOCOL_ERROR::NONE; // No error.
        }
    }
    else
    {
        if (urlParser->port.empty())
            urlParser->port = "23";

        // open client connection
        ret = open_client(urlParser->host, urlParser->getPort());
    }

    // call base class
    NetworkProtocol::open(urlParser, access, translate);

    return ret;
}

/**
 * @brief Close connection to the protocol.
 */
protocolError_t NetworkProtocolTCP::close()
{
    Debug_printf("NetworkProtocolTCP::close()\r\n");

    NetworkProtocol::close();

    if (client.connected())
    {
        Debug_printf("Closing client socket.\r\n");
        client.stop();
    }

    if (server != nullptr)
    {
        Debug_printf("Closing server socket.\r\n");
        server->stop();
    }

    return PROTOCOL_ERROR::NONE;
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param len number of bytes to read.
 * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
 */
protocolError_t NetworkProtocolTCP::read(unsigned short len)
{
    unsigned short actual_len = 0;
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolTCP::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        // Do the read from client socket.
        actual_len = client.read(newData.data(), len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NDEV_STATUS::CONNECTION_RESET;
            return PROTOCOL_ERROR::UNSPECIFIED;
        }
        else if (actual_len != len) // Read was short and timed out.
        {
            Debug_printf("Short receive. We got %u bytes, returning %u bytes and ERROR\r\n", actual_len, len);
            error = NDEV_STATUS::SOCKET_TIMEOUT;
            return PROTOCOL_ERROR::UNSPECIFIED;
        }

        // Add new data to buffer.
        receiveBuffer->insert(receiveBuffer->end(), newData.begin(), newData.end());
    }
    error = NDEV_STATUS::SUCCESS;
    return NetworkProtocol::read(len);
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return Number of bytes written.
 */
protocolError_t NetworkProtocolTCP::write(unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolTCP::write(%u)\r\n", len);

    // Check for client connection
    if (!client.connected())
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return PROTOCOL_ERROR::UNSPECIFIED; // error
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    // Do the write to client socket.
    actual_len = client.write((uint8_t *)transmitBuffer->data(), len);

    // bail if the connection is reset.
    if (errno == ECONNRESET)
    {
        error = NDEV_STATUS::CONNECTION_RESET;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }
    else if (actual_len != len) // write was short.
    {
        Debug_printf("Short send. We sent %u bytes, but asked to send %u bytes.\r\n", actual_len, len);
        error = NDEV_STATUS::SOCKET_TIMEOUT;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    // Return success
    error = NDEV_STATUS::SUCCESS;
    transmitBuffer->erase(0, len);

    return PROTOCOL_ERROR::NONE;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
 */
protocolError_t NetworkProtocolTCP::status(NetworkStatus *status)
{
    if (connectionIsServer == true)
        status_server(status);
    else
        status_client(status);

    NetworkProtocol::status(status);

    return PROTOCOL_ERROR::NONE;
}

void NetworkProtocolTCP::status_client(NetworkStatus *status)
{
    status->connected = client.connected();
    status->error = client.connected() ? error : NDEV_STATUS::END_OF_FILE;
}

void NetworkProtocolTCP::status_server(NetworkStatus *status)
{
    if (client.connected())
        status_client(status);
    else
    {
        status->connected = server->hasClient();
        status->error = error;
        Debug_printf("TCP::status_server C:%d E:%d\n", status->connected, (int) status->error);
    }
}

size_t NetworkProtocolTCP::available()
{
    size_t avail = receiveBuffer->size();
    if (!avail)
        avail = client.available();
    return avail;
}

/**
 * Open a server (listening) connection.
 * @param port bind to port #
 * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
 */
protocolError_t NetworkProtocolTCP::open_server(unsigned short port)
{
    Debug_printf("Binding to port %d\r\n", port);

    server = new fnTcpServer(port);
    int res = server->begin(port);
    connectionIsServer = true;      // set even if we're in error
    if (res == 0)
    {
        Debug_printf("errno = %u\r\n", errno);
        errno_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    return PROTOCOL_ERROR::NONE;
}

/**
 * Open a client connection to host and port.
 * @param hostname The hostname to connect to.
 * @param port the port number to connect to.
 * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
 */
protocolError_t NetworkProtocolTCP::open_client(std::string hostname, unsigned short port)
{
    int res = 0;

    connectionIsServer = false;

    Debug_printf("Connecting to host %s port %d\r\n", hostname.c_str(), port);

#ifdef ESP_PLATFORM
    res = client.connect(hostname.c_str(), port);
#else
    res = client.connect(hostname.c_str(), port, 5000); // TODO constant for connect timeout
#endif

    if (res == 0)
    {
        errno_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED; // Error.
    }
    else
        return PROTOCOL_ERROR::NONE; // We're connected.
}

/**
 * Accept a server connection, transfer to client socket.
 */
protocolError_t NetworkProtocolTCP::accept_connection()
{
    if (server == nullptr)
    {
        Debug_printf("Attempted accept connection on NULL server socket. Aborting.\r\n");
        error = NDEV_STATUS::SERVER_NOT_RUNNING;
        return PROTOCOL_ERROR::UNSPECIFIED; // Error
    }

    if (server->hasClient())
    {
        in_addr_t remoteIP;
        unsigned char remotePort;
        char *remoteIPString;

        client = server->client();

        if (client.connected())
        {
            remoteIP = client.remoteIP();
            remotePort = client.remotePort();
            remoteIPString = compat_inet_ntoa(remoteIP);
            Debug_printf("Accepted connection from %s:%u\r\n", remoteIPString, remotePort);
            return PROTOCOL_ERROR::NONE;
        }
        else
        {
            error = NDEV_STATUS::CONNECTION_RESET;
            Debug_printf("Client immediately disconnected.\r\n");
            return PROTOCOL_ERROR::UNSPECIFIED;
        }
    }

    // Otherwise, we are calling accept on a connection that isn't available.
    error = NDEV_STATUS::NO_CONNECTION_WAITING;
    return PROTOCOL_ERROR::UNSPECIFIED;
}

/**
 * Close client connection.
 */
protocolError_t NetworkProtocolTCP::close_client_connection()
{
    in_addr_t remoteIP;
    unsigned char remotePort;
    char *remoteIPString;

    if (server == nullptr)
    {
        Debug_printf("Attempted close client connection on NULL server socket. Aborting.\r\n");
        error = NDEV_STATUS::SERVER_NOT_RUNNING;
        return PROTOCOL_ERROR::NONE;
    }

    if (!client.connected())
    {
        Debug_printf("Attempted close client with no client connected.\r\n");
        error = NDEV_STATUS::NOT_CONNECTED;
        return PROTOCOL_ERROR::NONE;
    }

    remoteIP = client.remoteIP();
    remotePort = client.remotePort();
    remoteIPString = compat_inet_ntoa(remoteIP);

    Debug_printf("Disconnecting client %s:%u\r\n", remoteIPString, remotePort);

    // Clear all buffers.
    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    client.stop();

    return PROTOCOL_ERROR::UNSPECIFIED;
}
