/**
 * NetworkProtocolTCPS
 *
 * TCP over TLS Protocol Adapter Implementation
 */

#include "TCPS.h"

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
 * @return a NetworkProtocolTCPS object
 */
NetworkProtocolTCPS::NetworkProtocolTCPS(std::string *rx_buf, std::string *tx_buf,
                                         std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTCPS::ctor\r\n");
}

/**
 * dtor
 */
NetworkProtocolTCPS::~NetworkProtocolTCPS()
{
    Debug_printf("NetworkProtocolTCPS::dtor\r\n");
    tcp_tls_conn.stop();
}

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
netProtoErr_t NetworkProtocolTCPS::open(PeoplesUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    netProtoErr_t ret = NETPROTO_ERR_UNSPECIFIED; // assume error until proven ok

    Debug_printf("NetworkProtocolTCPS::open(%s:%s)\r\n", urlParser->host.c_str(),
                 urlParser->port.c_str());

    if (urlParser->host.empty())
    {
        // Open server on port, otherwise, treat as empty socket.
        if (!urlParser->port.empty())
            ret = open_server(urlParser->getPort());
        else
        {
            ret = NETPROTO_ERR_NONE; // No error.
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
    NetworkProtocol::open(urlParser, cmdFrame);

    return ret;
}

/**
 * @brief Close connection to the protocol.
 */
netProtoErr_t NetworkProtocolTCPS::close()
{
    Debug_printf("NetworkProtocolTCPS::close()\r\n");
    NetworkProtocol::close();
    tcp_tls_conn.stop();
    return NETPROTO_ERR_NONE;
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded
 * to length.
 * @param len number of bytes to read.
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::read(unsigned short len)
{
    unsigned short actual_len = 0;
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolTCPS::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        // Check for client connection
        if (!tcp_tls_conn.connected())
        {
            error = NETWORK_ERROR_NOT_CONNECTED;
            return NETPROTO_ERR_UNSPECIFIED; // error
        }

        // Do the read from client socket.
        actual_len = tcp_tls_conn.read(newData.data(), len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            return NETPROTO_ERR_UNSPECIFIED;
        }
        else if (actual_len != len) // Read was short and timed out.
        {
            error = NETWORK_ERROR_SOCKET_TIMEOUT;
            return NETPROTO_ERR_UNSPECIFIED;
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
netProtoErr_t NetworkProtocolTCPS::write(unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolTCPS::write(%u)\r\n", len);

    // Check for client connection
    if (!tcp_tls_conn.connected())
    {
        error = NETWORK_ERROR_NOT_CONNECTED;
        return NETPROTO_ERR_UNSPECIFIED; // error
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    // Do the write to client socket.
    actual_len = tcp_tls_conn.write((uint8_t *)transmitBuffer->data(), len);

    // bail if the connection is reset.
    if (errno == ECONNRESET)
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        return NETPROTO_ERR_UNSPECIFIED;
    }
    else if (actual_len != len) // write was short.
    {
        Debug_printf("NetworkProtocolTCPS: Short send. We sent %u bytes, but asked to send %u "
                     "bytes.\r\n",
                     actual_len, len);
        error = NETWORK_ERROR_SOCKET_TIMEOUT;
        return NETPROTO_ERR_UNSPECIFIED;
    }

    // Return success
    error = 1;
    transmitBuffer->erase(0, len);

    return NETPROTO_ERR_NONE;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::status(NetworkStatus *status)
{
    if (connectionIsServer == true)
        status_server(status);
    else
        status_client(status);

    NetworkProtocol::status(status);

    return NETPROTO_ERR_NONE;
}

void NetworkProtocolTCPS::status_client(NetworkStatus *status)
{
    status->connected = tcp_tls_conn.connected();
    status->error = tcp_tls_conn.connected() ? error : 136;
}

void NetworkProtocolTCPS::status_server(NetworkStatus *status)
{
    if (tcp_tls_conn.connected())
        status_client(status);
    else
    {
        status->connected = tcp_tls_conn.hasClient();
        status->error = error;
    }
}

size_t NetworkProtocolTCPS::available()
{
    if (!tcp_tls_conn.connected())
        return 0;
    size_t avail = receiveBuffer->size();
    if (!avail)
        avail = tcp_tls_conn.available();
    return avail;
}

/**
 * @brief Return a DSTATS byte for a requested COMMAND byte.
 * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
 * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF =
 * Command not supported.
 */
AtariSIODirection NetworkProtocolTCPS::special_inquiry(fujiCommandID_t cmd)
{
    Debug_printf("NetworkProtocolTCPS::special_inquiry(%02x)\r\n", cmd);

    switch (cmd)
    {
    case FUJICMD_CONTROL:
        return SIO_DIRECTION_NONE;
    case FUJICMD_CLOSE_CLIENT:
        return SIO_DIRECTION_NONE;
    default:
        break;
    }

    return SIO_DIRECTION_INVALID;
}

/**
 * @brief execute a command that returns no payload
 * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::special_00(cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolTCPS::special_00(%c)\n", cmdFrame->comnd);

    switch (cmdFrame->comnd)
    {
    case FUJICMD_CONTROL:
        return special_accept_connection();
        break;
    case FUJICMD_CLOSE_CLIENT:
        Debug_printf("NetworkProtocolTCPS: Closing client connection\r\n");
        return special_close_client_connection();
        break;
    }
    return NETPROTO_ERR_UNSPECIFIED; // error
}

/**
 * @brief execute a command that returns a payload to the atari.
 * @param sp_buf a pointer to the special buffer
 * @param len Length of data to request from protocol. Should not be larger than buffer.
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::special_40(uint8_t *sp_buf, unsigned short len,
                                              cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

/**
 * @brief execute a command that sends a payload to fujinet (most common, XIO)
 * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
 * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
 */
netProtoErr_t NetworkProtocolTCPS::special_80(uint8_t *sp_buf, unsigned short len,
                                              cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

/**
 * Open a server (listening) connection.
 * @param port bind to port #
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::open_server(unsigned short port)
{
    Debug_printf("NetworkProtocolTCPS: Binding to port %d\r\n", port);

    // server = new fnTcpsConnection((uint16_t)port);
    int res = tcp_tls_conn.begin_listening((uint16_t)port);
    connectionIsServer = true; // set even if we're in error
    if (res == 0)
    {
        Debug_printf("NetworkProtocolTCPS: errno = %u\r\n", errno);
        errno_to_error();
        return NETPROTO_ERR_UNSPECIFIED;
    }

    return NETPROTO_ERR_NONE;
}

/**
 * Open a client connection to host and port.
 * @param hostname The hostname to connect to.
 * @param port the port number to connect to.
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocolTCPS::open_client(std::string hostname, unsigned short port)
{
    int res = 0;

    connectionIsServer = false;

    Debug_printf("Connecting to host %s port %d\r\n", hostname.c_str(), port);

#ifdef ESP_PLATFORM
    res = tcp_tls_conn.connect(hostname.c_str(), port);
#else
    res = tcp_tls_conn.connect(hostname.c_str(), port,
                               5000); // TODO constant for connect timeout
#endif

    if (res == 0)
    {
        errno_to_error();
        return NETPROTO_ERR_UNSPECIFIED; // Error.
    }
    else
        return NETPROTO_ERR_NONE; // We're connected.
}

/**
 * Special: Accept a server connection, transfer to client socket.
 */
netProtoErr_t NetworkProtocolTCPS::special_accept_connection()
{
    if (!tcp_tls_conn.hasClient())
    {
        Debug_printf("NetworkProtocolTCPS: Attempted accept without a client connection.\r\n");
        error = NETWORK_ERROR_SERVER_NOT_RUNNING;
        return NETPROTO_ERR_UNSPECIFIED; // Error
    }

    int res = 1;

    if (tcp_tls_conn.hasClient())
    {
        in_addr_t remoteIP;
        unsigned char remotePort;
        char *remoteIPString;

        remoteIP = tcp_tls_conn.remoteIP();
        remotePort = tcp_tls_conn.remotePort();
        remoteIPString = compat_inet_ntoa(remoteIP);
        res = tcp_tls_conn.accept_connection();
        if (res == 0)
        {
            Debug_printf("NetworkProtocolTCPS: Accepted connection from %s:%u\r\n",
                         remoteIPString, remotePort);
            return NETPROTO_ERR_NONE;
        }
        else
        {
            return NETPROTO_ERR_UNSPECIFIED;
        }
    }
    else
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        Debug_printf("NetworkProtocolTCPS: Client immediately disconnected.\r\n");
        return NETPROTO_ERR_UNSPECIFIED;
    }
}

/**
 * Special: Close connection .
 */
netProtoErr_t NetworkProtocolTCPS::special_close_client_connection()
{
    tcp_tls_conn.stop();

    // Clear all buffers.
    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();

    return NETPROTO_ERR_UNSPECIFIED; // this seems wrong?
}
