/**
 * NetworkProtocolTELNET
 * 
 * TELNET Protocol Adapter Implementation
 */

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "Telnet.h"
#include "status_error_codes.h"
#include "libtelnet.h"

static const telnet_telopt_t telopts[] = {
    {TELNET_TELOPT_ECHO, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_TTYPE, TELNET_WILL, TELNET_DONT},
    {TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_MSSP, TELNET_WONT, TELNET_DO},
    {-1, 0, 0}};

static telnet_t *telnet;

static void _event_handler(telnet_t *telnet, telnet_event_t *ev, void *user_data)
{
    NetworkProtocolTELNET *protocol = (NetworkProtocolTELNET *)user_data;

    switch (ev->type)
    {
    case TELNET_EV_DATA:
        break;
    case TELNET_EV_SEND:
        break;
    case TELNET_EV_WILL:
        break;
    case TELNET_EV_WONT:
        break;
    case TELNET_EV_DO:
        break;
    case TELNET_EV_DONT:
        break;
    case TELNET_EV_TTYPE:
        break;
    case TELNET_EV_SUBNEGOTIATION:
        break;
    case TELNET_EV_ERROR:
        break;
    default:
        break;
    }
}

/**
 * ctor
 */
NetworkProtocolTELNET::NetworkProtocolTELNET(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTELNET::ctor\n");
    server = nullptr;
    telnet = telnet_init(telopts, _event_handler, 0, this);
}

/**
 * dtor
 */
NetworkProtocolTELNET::~NetworkProtocolTELNET()
{
    Debug_printf("NetworkProtocolTELNET::dtor\n");

    if (server != nullptr)
    {
        delete server;

        server = nullptr;
    }

    if (telnet != nullptr)
    {
        telnet_free(telnet);
    }
}

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
bool NetworkProtocolTELNET::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    bool ret = true; // assume error until proven ok

    if (urlParser->port.empty())
        urlParser->port = "23";

    Debug_printf("NetworkProtocolTELNET::open(%s:%s)", urlParser->hostName.c_str(), urlParser->port.c_str());

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

    // call base class
    NetworkProtocol::open(urlParser, cmdFrame);

    return ret;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocolTELNET::close()
{
    Debug_printf("NetworkProtocolTELNET::close()\n");

    NetworkProtocol::close();

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

    return false;
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param len number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTELNET::read(unsigned short len)
{
    unsigned short actual_len = 0;
    uint8_t *newData = (uint8_t *)malloc(len);
    string newString;

    Debug_printf("NetworkProtocolTELNET::read(%u)\n", len);

    if (newData == nullptr)
    {
        Debug_printf("Could not allocate %u bytes! Aborting!\n");
        return true; // error.
    }

    if (receiveBuffer->length() == 0)
    {
        // Check for client connection
        if (!client.connected())
        {
            error = NETWORK_ERROR_NOT_CONNECTED;
            return true; // error
        }

        // Do the read from client socket.
        actual_len = client.read(newData, len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            return true;
        }
        else if (actual_len != len) // Read was short and timed out.
        {
            Debug_printf("Short receive. We got %u bytes, returning %u bytes and ERROR\n", actual_len, len);
            error = NETWORK_ERROR_SOCKET_TIMEOUT;
            return true;
        }

        // Add new data to buffer.
        newString = string((char *)newData, len);
        *receiveBuffer += newString;

        free(newData);
    }
    // Return success
    error = 1;
    return NetworkProtocol::read(len);
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return Number of bytes written.
 */
bool NetworkProtocolTELNET::write(unsigned short len)
{
    int actual_len = 0;

    Debug_printf("NetworkProtocolTELNET::write(%u)\n", len);

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
        Debug_printf("Short send. We sent %u bytes, but asked to send %u bytes.\n", actual_len, len);
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
bool NetworkProtocolTELNET::status(NetworkStatus *status)
{
    if (connectionIsServer == true)
        status_server(status);
    else
        status_client(status);

    NetworkProtocol::status(status);

    return false;
}

void NetworkProtocolTELNET::status_client(NetworkStatus *status)
{
    status->rxBytesWaiting = (client.available() > 65535) ? 65535 : client.available();
    status->reserved = client.connected();
    status->error = client.connected() ? error : 136;
}

void NetworkProtocolTELNET::status_server(NetworkStatus *status)
{
    if (client.connected())
        status_client(status);
    else
    {
        status->reserved = server->hasClient();
        status->error = error;
    }
}

/**
 * @brief Return a DSTATS byte for a requested COMMAND byte.
 * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
 * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
 */
uint8_t NetworkProtocolTELNET::special_inquiry(uint8_t cmd)
{
    Debug_printf("NetworkProtocolTELNET::special_inquiry(%02x)\n", cmd);

    switch (cmd)
    {
    case 'A':
        return 0x00;
    }

    return 0xFF;
}

/**
 * @brief execute a command that returns no payload
 * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
 * @return error flag. TRUE on error, FALSE on success.
 */
bool NetworkProtocolTELNET::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    case 'A':
        return special_accept_connection();
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
bool NetworkProtocolTELNET::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

/**
 * @brief execute a command that sends a payload to fujinet (most common, XIO)
 * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
 * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
 */
bool NetworkProtocolTELNET::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

/**
 * Open a server (listening) connection.
 * @param port bind to port #
 * @return error flag. TRUE on error. FALSE on success.
 */
bool NetworkProtocolTELNET::open_server(unsigned short port)
{
    Debug_printf("Binding to port %d\n", port);

    server = new fnTcpServer(port);
    server->begin(port);
    connectionIsServer = true;

    Debug_printf("errno = %u\n", errno);

    return errno != 0;
}

/**
 * Open a client connection to host and port.
 * @param hostname The hostname to connect to.
 * @param port the port number to connect to.
 * @return error flag. TRUE on erorr. FALSE on success.
 */
bool NetworkProtocolTELNET::open_client(string hostname, unsigned short port)
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

/**
 * Special: Accept a server connection, transfer to client socket.
 */
bool NetworkProtocolTELNET::special_accept_connection()
{
    if (server == nullptr)
    {
        Debug_printf("Attempted accept connection on NULL server socket. Aborting.\n");
        return true; // Error
    }

    if (server->hasClient())
    {
        in_addr_t remoteIP = client.remoteIP();
        unsigned char remotePort = client.remotePort();
        char *remoteIPString = inet_ntoa(remoteIP);

        client = server->available();

        if (client.connected())
        {
            Debug_printf("Accepted connection from %s:%u", remoteIPString, remotePort);
            return false;
        }
        else
        {
            Debug_printf("Client immediately disconnected.\n");
        }
    }

    return true;
}