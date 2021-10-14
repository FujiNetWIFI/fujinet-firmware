#ifdef BUILD_ATARI // temporary

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

    string *receiveBuffer = protocol->getReceiveBuffer();

    if (protocol == nullptr)
    {
        Debug_printf("_event_handler() - NULL TELNET Protocol handler!\n");
        return;
    }

    Debug_printf("_event_handler(%d)\n", ev->type);

    switch (ev->type)
    {
    case TELNET_EV_DATA: // Received Data
        *receiveBuffer += string(ev->data.buffer, ev->data.size);
        protocol->newRxLen = receiveBuffer->size();
        Debug_printf("Received TELNET DATA: %s\n", receiveBuffer->c_str());
        break;
    case TELNET_EV_SEND:
        Debug_printf("Sending: ");
        for (int i = 0; i < ev->data.size; i++)
        {
            Debug_printf("%02x ", ev->data.buffer[i]);
        }
        Debug_printf("\n");
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
            telnet_ttype_is(telnet, "dumb");
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
    : NetworkProtocolTCP(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolTELNET::ctor\n");
    server = nullptr;
    telnet = telnet_init(telopts, _event_handler, 0, this);
    newRxLen = 0;
}

/**
 * dtor
 */
NetworkProtocolTELNET::~NetworkProtocolTELNET()
{
    Debug_printf("NetworkProtocolTELNET::dtor\n");

    if (telnet != nullptr)
    {
        telnet_free(telnet);
    }
    newRxLen = 0;
}


/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param len number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTELNET::read(unsigned short len)
{
    char *newData = (char *)malloc(len);

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
        client.read((uint8_t *)newData, len);

        telnet_recv(telnet, newData, len);

        // bail if the connection is reset.
        if (errno == ECONNRESET)
        {
            error = NETWORK_ERROR_CONNECTION_RESET;
            return true;
        }

        free(newData);
    }
    // Return success
    error = 1;

    Debug_printf("NetworkProtocolTelnet::read(%d) - %s\n", newRxLen, receiveBuffer->c_str());

    return NetworkProtocol::read(newRxLen); // Set by calls into telnet_recv()
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return Number of bytes written.
 */
bool NetworkProtocolTELNET::write(unsigned short len)
{
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
    telnet_send(telnet, transmitBuffer->data(), len);

    // bail if the connection is reset.
    if (errno == ECONNRESET)
    {
        error = NETWORK_ERROR_CONNECTION_RESET;
        return len;
    }

    // Return success
    error = 1;

    return false;
}

void NetworkProtocolTELNET::flush(const char *buf, unsigned short size)
{
    client.write((uint8_t *)buf, size);
    transmitBuffer->clear();
}

#endif /* BUILD_ATARI */