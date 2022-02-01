/**
 * Network Protocol Base class
 */

#include <string.h>
#include <algorithm>
#include "Protocol.h"
#include "status_error_codes.h"
#include "../utils/utils.h"

#include "../../include/debug.h"

using namespace std;

#define ASCII_BELL 0x07
#define ASCII_BACKSPACE 0x08
#define ASCII_TAB 0x09
#define ASCII_LF 0x0A
#define ASCII_CR 0x0D
#define ATASCII_EOL 0x9B
#define ATASCII_DEL 0x7E
#define ATASCII_TAB 0x7F
#define ATASCII_BUZZER 0xFD

#define TRANSLATION_MODE_NONE 0
#define TRANSLATION_MODE_CR 1
#define TRANSLATION_MODE_LF 2
#define TRANSLATION_MODE_CRLF 3

/**
 * ctor - Initialize network protocol object.
 * @param rx_buf pointer to receive buffer
 * @param tx_buf pointer to transmit buffer
 * @param sp_buf pointer to special buffer
 */
NetworkProtocol::NetworkProtocol(string *rx_buf,
                                 string *tx_buf,
                                 string *sp_buf)
{
    Debug_printf("NetworkProtocol::ctor()\n");

    receiveBuffer = rx_buf;
    transmitBuffer = tx_buf;
    specialBuffer = sp_buf;
    error = 1;
    login = password = nullptr;
}

/**
 * dtor - clean up after network protocol object
 */
NetworkProtocol::~NetworkProtocol()
{
    Debug_printf("NetworkProtocol::dtor()\n");
    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
    receiveBuffer = nullptr;
    transmitBuffer = nullptr;
    specialBuffer = nullptr;
}

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
bool NetworkProtocol::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    // Set translation mode, Bits 0-2 of aux2
    translation_mode = cmdFrame->aux2 & 0x03;

    // Persist aux1/aux2 values for later.
    aux1_open = cmdFrame->aux1;
    aux2_open = cmdFrame->aux2;

    opened_url = urlParser;

    return false;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocol::close()
{
    if (!transmitBuffer->empty())
        write(transmitBuffer->length());

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
    error = 1;
    return false;
}

/**
 * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
 * @param len Number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::read(unsigned short len)
{
    Debug_printf("NetworkProtocol::read(%u)\n", len);
    translate_receive_buffer();
    error = 1;
    return false;
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::write(unsigned short len)
{
    return false;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @param rx_buf a pointer to the receive buffer (to call read())
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::status(NetworkStatus *status)
{
    if (receiveBuffer->length() == 0 && status->rxBytesWaiting > 0)
        read(status->rxBytesWaiting);

    status->rxBytesWaiting = receiveBuffer->length();

    return false;
}

/**
 * Perform end of line translation on receive buffer. based on translation_mode.
 * @param rx_buf The receive buffer to transform
 * @param len The length of the receive buffer
 * @return length after transformation.
  */
void NetworkProtocol::translate_receive_buffer()
{
    if (translation_mode == 0)
        return;

    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BELL, ATASCII_BUZZER);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BACKSPACE, ATASCII_DEL);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_TAB, ATASCII_TAB);

    switch (translation_mode)
    {
    case TRANSLATION_MODE_CR:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, ATASCII_EOL);
        break;
    case TRANSLATION_MODE_LF:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_LF, ATASCII_EOL);
        break;
    case TRANSLATION_MODE_CRLF:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, ATASCII_EOL);
        break;
    }

    if (translation_mode == TRANSLATION_MODE_CRLF)
        receiveBuffer->erase(std::remove(receiveBuffer->begin(), receiveBuffer->end(), '\n'), receiveBuffer->end());
}

/**
 * Perform end of line translation on transmit buffer. based on translation_mode
 * @return new length after translation
 */
unsigned short NetworkProtocol::translate_transmit_buffer()
{
    if (translation_mode == 0)
        return transmitBuffer->length();

    replace(transmitBuffer->begin(), transmitBuffer->end(), ATASCII_BUZZER, ASCII_BELL);
    replace(transmitBuffer->begin(), transmitBuffer->end(), ATASCII_DEL, ASCII_BACKSPACE);
    replace(transmitBuffer->begin(), transmitBuffer->end(), ATASCII_TAB, ASCII_TAB);

    switch (translation_mode)
    {
    case TRANSLATION_MODE_CR:
        replace(transmitBuffer->begin(), transmitBuffer->end(), ATASCII_EOL, ASCII_CR);
        break;
    case TRANSLATION_MODE_LF:
        replace(transmitBuffer->begin(), transmitBuffer->end(), ATASCII_EOL, ASCII_LF);
        break;
    case TRANSLATION_MODE_CRLF:
        util_replaceAll(*transmitBuffer, "\x9b", "\x0d\x0a");
        break;
    }

    return transmitBuffer->length();
}

/**
 * Map errno to error
 */
void NetworkProtocol::errno_to_error()
{
    switch (errno)
    {
    case EAGAIN:
        error = 1; // This is okay.
        errno = 0; // Short circuit and say it's okay.
        break;
    case EADDRINUSE:
        error = NETWORK_ERROR_ADDRESS_IN_USE;
        break;
    case EINPROGRESS:
        error = NETWORK_ERROR_CONNECTION_ALREADY_IN_PROGRESS;
        break;
    case ECONNRESET:
        error = NETWORK_ERROR_CONNECTION_RESET;
        break;
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
    default:
        Debug_printf("errno_to_error() - Uncaught errno = %u, returning 144.\n", errno);
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}
