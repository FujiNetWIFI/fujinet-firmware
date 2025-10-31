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
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
bool NetworkProtocolTCP::open(PeoplesUrlParser *urlParser, FujiTranslationMode mode)
{
    bool ret = true; // assume error until proven ok

    Debug_printf("NetworkProtocolTCP::open(%s:%s)\r\n", urlParser->host.c_str(), urlParser->port.c_str());

    if (urlParser->host.empty())
    {
        // Open server on port, otherwise, treat as empty socket.
        if (!urlParser->port.empty())
            ret = open_server(urlParser->getPort());
        else
        {
            Debug_printf("Empty socket enabled.\r\n");
            ret = false; // No error.
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
    NetworkProtocol::open(urlParser, mode);

    return ret;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocolTCP::close()
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

    return false;
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param len number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::read(unsigned short len)
{
    unsigned short actual_len = 0;
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolTCP::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        // Check for client connection
        if (!client.connected())
        {
            error = NETWORK_ERROR_NOT_CONNECTED;
            return true; // error
        }

        // Do the read from client socket.
        actual_len = client.read(newData.data(), len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            return true;
        }
        else if (actual_len != len) // Read was short and timed out.
        {
            Debug_printf("Short receive. We got %u bytes, returning %u bytes and ERROR\r\n", actual_len, len);
            error = NETWORK_ERROR_SOCKET_TIMEOUT;
            return true;
        }

        // Add new data to buffer.
        receiveBuffer->insert(receiveBuffer->end(), newData.begin(), newData.end());
    }
    error = 1;
    return NetworkProtocol::read(len);
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return Number of bytes written.
 */
bool NetworkProtocolTCP::write(unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolTCP::write(%u)\r\n", len);

    // Check for client connection
    if (!client.connected())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return len; // error
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    // Do the write to client socket.
    actual_len = client.write((uint8_t *)transmitBuffer->data(), len);

    // bail if the connection is reset.
    if (errno == ECONNRESET)
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        return len;
    }
    else if (actual_len != len) // write was short.
    {
        Debug_printf("Short send. We sent %u bytes, but asked to send %u bytes.\r\n", actual_len, len);
        error = NETWORK_ERROR_SOCKET_TIMEOUT;
        return true;
    }

    // Return success
    error = 1;
    transmitBuffer->erase(0, len);

    return false;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::status(NetworkStatus *status)
{
    if (connectionIsServer == true)
        status_server(status);
    else
        status_client(status);

    NetworkProtocol::status(status);

    return false;
}

void NetworkProtocolTCP::status_client(NetworkStatus *status)
{
#if 0
    status->rxBytesWaiting = (client.available() > 65535) ? 65535 : client.available();
#endif
    status->connected = client.connected();
    status->error = client.connected() ? error : 136;
}

void NetworkProtocolTCP::status_server(NetworkStatus *status)
{
    if (client.connected())
        status_client(status);
    else
    {
        status->connected = server->hasClient();
        status->error = error;
    }
}

size_t NetworkProtocolTCP::available()
{
    if (!client.connected())
        return 0;
    size_t avail = receiveBuffer->size();
    if (!avail)
        avail = client.available();
    return avail;
}

/**
 * @brief Return a DSTATS byte for a requested COMMAND byte.
 * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
 * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
 */
FujiDirection NetworkProtocolTCP::special_inquiry(uint8_t cmd)
{
    Debug_printf("NetworkProtocolTCP::special_inquiry(%02x)\r\n", cmd);

    switch (cmd)
    {
    case 'A':
        return DIRECTION_NONE;
    case 'c':
        return DIRECTION_NONE;
    }

    return DIRECTION_INVALID;
}

#ifdef OBSOLETE
/**
 * @brief execute a command that returns no payload
 * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
 * @return error flag. TRUE on error, FALSE on success.
 */
bool NetworkProtocolTCP::special_00(cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolTCP::special_00(%c)\n",cmdFrame->comnd);

    switch (cmdFrame->comnd)
    {
    case 'A':
        return special_accept_connection();
        break;
    case 'c':
        Debug_printf("CLOSING CLIENT CONNECTION!!!\n");
        return special_close_client_connection();
        break;
    }
    return true; // error
}

/**
 * @brief execute a command that returns a payload to the atari.
 * @param sp_buf a pointer to the special buffer
 * @param len Length of data to request from protocol. Should not be larger than buffer.
 * @return error flag. TRUE on error, FALSE on success.
 */
bool NetworkProtocolTCP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

/**
 * @brief execute a command that sends a payload to fujinet (most common, XIO)
 * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
 * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
 */
bool NetworkProtocolTCP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}
#endif /* OBSOLETE */

/**
 * Open a server (listening) connection.
 * @param port bind to port #
 * @return error flag. TRUE on error. FALSE on success.
 */
bool NetworkProtocolTCP::open_server(unsigned short port)
{
    Debug_printf("Binding to port %d\r\n", port);

    server = new fnTcpServer(port);
    int res = server->begin(port);
    connectionIsServer = true;      // set even if we're in error
    if (res == 0)
    {
        Debug_printf("errno = %u\r\n", errno);
        errno_to_error();
        return true;
    }

    return false;
}

/**
 * Open a client connection to host and port.
 * @param hostname The hostname to connect to.
 * @param port the port number to connect to.
 * @return error flag. TRUE on erorr. FALSE on success.
 */
bool NetworkProtocolTCP::open_client(std::string hostname, unsigned short port)
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
        return true; // Error.
    }
    else
        return false; // We're connected.
}

/**
 * Special: Accept a server connection, transfer to client socket.
 */
bool NetworkProtocolTCP::special_accept_connection()
{
    if (server == nullptr)
    {
        Debug_printf("Attempted accept connection on NULL server socket. Aborting.\r\n");
        error = NETWORK_ERROR_SERVER_NOT_RUNNING;
        return true; // Error
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
            return false;
        }
        else
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            Debug_printf("Client immediately disconnected.\r\n");
            return true;
        }
    }

    // Otherwise, we are calling accept on a connection that isn't available.
    error = NETWORK_ERROR_NO_CONNECTION_WAITING;
    return true;
}

/**
 * Special: Accept a server connection, transfer to client socket.
 */
bool NetworkProtocolTCP::special_close_client_connection()
{
    in_addr_t remoteIP;
    unsigned char remotePort;
    char *remoteIPString;

    if (server == nullptr)
    {
        Debug_printf("Attempted close client connection on NULL server socket. Aborting.\r\n");
        error = NETWORK_ERROR_SERVER_NOT_RUNNING;
        return false;
    }

    if (!client.connected())
    {
        Debug_printf("Attempted close client with no client connected.\r\n");
        error = NETWORK_ERROR_NOT_CONNECTED;
        return false;
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

    return false;
}
