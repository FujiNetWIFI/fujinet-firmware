/**
 * NetworkProtocolTELNET
 *
 * TELNET Protocol Adapter Implementation
 */

#include "Telnet.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "compat_inet.h"

#include "../../include/debug.h"

#include "libtelnet.h"
#include "status_error_codes.h"

#include <vector>


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

    if (protocol == nullptr)
    {
        Debug_printf("_event_handler() - NULL TELNET Protocol handler!\r\n");
        return;
    }

    std::string *receiveBuffer = protocol->getReceiveBuffer();

    switch (ev->type)
    {
    case TELNET_EV_DATA: // Received Data
        *receiveBuffer += std::string(ev->data.buffer, ev->data.size);
        protocol->newRxLen = receiveBuffer->size();
        break;
    case TELNET_EV_SEND:
        protocol->flush(ev->data.buffer, ev->data.size);
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
        if (ev->ttype.cmd == TELNET_TTYPE_SEND)
            telnet_ttype_is(telnet, protocol->ttype);
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
NetworkProtocolTELNET::NetworkProtocolTELNET(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolTCP(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTELNET::ctor\r\n");
    server = nullptr;
    telnet = telnet_init(telopts, _event_handler, 0, this);
    newRxLen = 0;
}

/**
 * dtor
 */
NetworkProtocolTELNET::~NetworkProtocolTELNET()
{
    Debug_printf("NetworkProtocolTELNET::dtor\r\n");

    if (telnet != nullptr)
    {
        telnet_free(telnet);
    }
    newRxLen = 0;
}


/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param len number of bytes to read.
 * @return PROTOCOL_ERROR_NONE on success, PROTOCOL_ERROR_UNSPECIFIED on error
 */
protocolError_t NetworkProtocolTELNET::read(unsigned short len)
{
    std::vector<uint8_t> newData = std::vector<uint8_t>(len);

    Debug_printf("NetworkProtocolTELNET::read(%u)\r\n", len);

    if (receiveBuffer->length() == 0)
    {
        // Check for client connection
        if (!client.connected())
        {
            error = NDEV_STATUS::NOT_CONNECTED;
            return PROTOCOL_ERROR::UNSPECIFIED; // error
        }

        // Do the read from client socket.
        client.read(newData.data(), len);

        telnet_recv(telnet, (char *)newData.data(), len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NDEV_STATUS::CONNECTION_RESET;
            return PROTOCOL_ERROR::UNSPECIFIED;
        }
    }

    // Return success
    error = NDEV_STATUS::SUCCESS;

    Debug_printf("NetworkProtocolTELNET::read(%d) - %s\r\n", newRxLen, receiveBuffer->c_str());

    return NetworkProtocol::read(newRxLen); // Set by calls into telnet_recv()
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return Number of bytes written.
 */
protocolError_t NetworkProtocolTELNET::write(unsigned short len)
{
    Debug_printf("NetworkProtocolTELNET::write(%u)\r\n", len);

    // Check for client connection
    if (!client.connected())
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        return PROTOCOL_ERROR::UNSPECIFIED; // error
    }

    // Call base class to do translation.
    len = translate_transmit_buffer();

    // Do the write to client socket.
    telnet_send(telnet, transmitBuffer->data(), len);

    // bail if the connection is reset.
    if (errno == ECONNRESET)
    {
        error = NDEV_STATUS::CONNECTION_RESET;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    // Return success
    error = NDEV_STATUS::SUCCESS;

    return PROTOCOL_ERROR::NONE;
}

void NetworkProtocolTELNET::flush(const char *buf, unsigned short size)
{
    client.write((uint8_t *)buf, size);
    transmitBuffer->clear();
}
