/**
 * NetworkProtocolTCP
 * 
 * TCP Protocol Adapter Implementation
 */

#include <errno.h>
#include <string.h>
#include "TCP.h"
#include "status_error_codes.h"

/**
 * ctor
 */
NetworkProtocolTCP::NetworkProtocolTCP()
{
    Debug_printf("NetworkProtocolTCP::ctor\n");
    server = nullptr;
}

/**
 * dtor
 */
NetworkProtocolTCP::~NetworkProtocolTCP()
{
    Debug_printf("NetworkProtocolTCP::dtor\n");

    if (server != nullptr)
    {
        if (client.connected())
            client.stop();

        server->stop();

        delete server;

        server = nullptr;
    }
}

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
bool NetworkProtocolTCP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    bool ret = true; // assume error until proven ok

    if (urlParser->port.empty())
        urlParser->port = "23";

    Debug_printf("NetworkProtocolTCP::open(%s:%s)", urlParser->hostName.c_str(), urlParser->port.c_str());

    if (urlParser->hostName.empty())
    {
        // open server on port
        ret = open_server(atoi(urlParser->port.c_str()));
    }
    else
    {
        // open client connection
        ret = open_client(urlParser->hostName, atoi(urlParser->port.c_str()));
    }

    return ret;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocolTCP::close()
{
    Debug_printf("NetworkProtocolTCP::close()\n");

    if (client.connected())
    {
        Debug_printf("Closing client socket.\n");
        client.stop();
    }

    if (server != nullptr)
    {
        Debug_printf("Closing server socket.\n");
        server->stop();
    }

    return false; // Always successful./
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param rx_buf The destination buffer to accept received bytes from protocol.
 * @param len The # of bytes expected from protocol adapter. Buffer should be large enough.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::read(uint8_t *rx_buf, unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolTCP::read(%u)\n", len);

    // Clear buffer
    memset(rx_buf, 0, len);

    // Check for client connection
    if (client.connected())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return true; // error
    }

    // Do the read from client socket.
    actual_len = client.read(rx_buf, len);

    // bail if the connection is reset.
    if (errno==ECONNRESET)
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        return true;
    }
    else if (actual_len != len) // Read was short and timed out.
    {
        Debug_printf("Short receive. We got %u bytes, returning %u bytes and ERROR\n",actual_len,len);
        error=NETWORK_ERROR_SOCKET_TIMEOUT;
        return true;
    }

    // Return success
    return false;
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param tx_buf The buffer containing data to transmit.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::write(uint8_t *tx_buf, unsigned short len)
{
    int actual_len = 0;
    
    Debug_printf("NetworkProtocolTCP::write(%u)\n",len);

    // Check for client connection
    if (client.connected())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return true; // error
    }

    // Do the read from client socket.
    actual_len = client.write(tx_buf, len);

    // bail if the connection is reset.
    if (errno==ECONNRESET)
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        return true;
    }
    else if (actual_len != len) // write was short.
    {
        Debug_printf("Short send. We sent %u bytes, but asked to send %u bytes.\n",actual_len,len);
        error=NETWORK_ERROR_SOCKET_TIMEOUT;
        return true;
    }

    // Return success
    return false;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::status(NetworkStatus *status)
{
    return false;
}

/**
 * @brief Return a DSTATS byte for a requested COMMAND byte.
 * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
 * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
 */
uint8_t NetworkProtocolTCP::special_inquiry(uint8_t cmd)
{
    return 0xFF; // not implemented.
}

/**
 * @brief execute a command that returns no payload
 * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
 * @return error flag. TRUE on error, FALSE on success.
 */
bool NetworkProtocolTCP::special_00(cmdFrame_t *cmdFrame)
{
    return false;
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

/**
 * Open a server (listening) connection.
 * @param port bind to port #
 * @return error flag. TRUE on error. FALSE on success.
 */
bool NetworkProtocolTCP::open_server(unsigned short port)
{
    Debug_printf("Binding to port %d\n", port);

    server = new fnTcpServer(port);
    server->begin(port);
    connectionIsServer = true;

    return errno < 0;
}

/**
 * Open a client connection to host and port.
 * @param hostname The hostname to connect to.
 * @param port the port number to connect to.
 * @return error flag. TRUE on erorr. FALSE on success.
 */
bool NetworkProtocolTCP::open_client(string hostname, unsigned short port)
{
    int res = 0;

    connectionIsServer = false;

    Debug_printf("Connecting to host %s port %d\n", hostname.c_str(), port);

    res = client.connect(hostname.c_str(), port);

    if (res == 0)
    {
        // Did not connect.
        switch (errno)
        {
        case ECONNREFUSED:
            error = NETWORK_ERROR_CONNECTION_REFUSED;
            break;
        case ENETUNREACH:
            error = NETWORK_ERROR_NETWORK_UNREACHABLE;
            break;
        case ETIMEDOUT:
            error = NETWORK_ERROR_SOCKET_TIMEOUT;
            break;
        case ENETDOWN:
            error = NETWORK_ERROR_NETWORK_DOWN;
            break;
        }

        return true; // Error.
    }

    return false; // We're connected.
}