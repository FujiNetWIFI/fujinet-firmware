/**
 * Network Protocol Base class
 */

#include "Protocol.h"

#include <algorithm>
#include <errno.h>

#include "../../include/debug.h"

#include "compat_inet.h"
#include "status_error_codes.h"
#include "utils.h"
#include "string_utils.h"

#include <vector>


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

#define STR_ASCII_BELL "\x07"
#define STR_ASCII_BACKSPACE "\x08"
#define STR_ASCII_TAB "\x09"
#define STR_ASCII_LF "\x0a"
#define STR_ASCII_CR "\x0d"
#define STR_ASCII_CRLF "\x0d\x0a"
#define STR_ATASCII_EOL "\x9b"
#define STR_ATASCII_DEL "\x7e"
#define STR_ATASCII_TAB "\x7f"
#define STR_ATASCII_BUZZER "\xfd"

/**
 * NWD
 * We only have 2 bits for translations (see NetworkProtocol::open)
 * but we need to translate LF to CR or CRLF to just CR
 * The only solution is to change the behaviour of the Apple2
 * version.  It may make more sense to have the Atari be the odd
 * one in the future rather than the Apple2
 */

#ifdef BUILD_APPLE
#define EOL 0x0D
#define STR_EOL "\x0d"
#else
#define EOL 0x9B
#define STR_EOL "\x9b"
#endif

/**
 * ctor - Initialize network protocol object.
 * @param rx_buf pointer to receive buffer
 * @param tx_buf pointer to transmit buffer
 * @param sp_buf pointer to special buffer
 */
NetworkProtocol::NetworkProtocol(std::string *rx_buf,
                                 std::string *tx_buf,
                                 std::string *sp_buf)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocol::ctor()\r\n");
#endif
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
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocol::dtor()\r\n");
#endif
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
netProtoErr_t NetworkProtocol::open(PeoplesUrlParser *urlParser, netProtoOpenMode_t omode,
                                    netProtoTranslation_t translate)
{
    // Set translation mode, Bits 0-1 of aux2
    translation_mode = translate;
    opened_write = omode == NETPROTO_OPEN_WRITE;

    opened_url = urlParser;

    return NETPROTO_ERR_NONE;
}

void NetworkProtocol::set_open_params(netProtoTranslation_t mode)
{
    translation_mode = mode;
#ifdef VERBOSE_PROTOCOL
    Debug_printf("Set translation_mode to %d\r\n", translation_mode);
#endif
}

/**
 * @brief Close connection to the protocol.
 */
netProtoErr_t NetworkProtocol::close()
{
    if (!transmitBuffer->empty())
        write(transmitBuffer->length());

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
    receiveBuffer->shrink_to_fit();
    transmitBuffer->shrink_to_fit();
    specialBuffer->shrink_to_fit();

    error = 1;
    return NETPROTO_ERR_NONE;
}

/**
 * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
 * @param len Number of bytes to read.
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocol::read(unsigned short len)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocol::read(%u)\r\n", len);
#endif
    translate_receive_buffer();
    error = 1;
    return NETPROTO_ERR_NONE;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @param rx_buf a pointer to the receive buffer (to call read())
 * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
 */
netProtoErr_t NetworkProtocol::status(NetworkStatus *status)
{
    if (fromInterrupt)
        return NETPROTO_ERR_NONE;

    if (!is_write && receiveBuffer->length() == 0 && available() > 0)
        read(available());

    return NETPROTO_ERR_NONE;
}

/**
 * Perform end of line translation on receive buffer. based on translation_mode.
 * @param rx_buf The receive buffer to transform
 * @param len The length of the receive buffer
 * @return length after transformation.
  */
void NetworkProtocol::translate_receive_buffer()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("#### Translating receive buffer, mode: %u\r\n", translation_mode);
#endif
    if (translation_mode == 0)
        return;

    #ifdef BUILD_ATARI
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BELL, ATASCII_BUZZER);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BACKSPACE, ATASCII_DEL);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_TAB, ATASCII_TAB);
    #endif

    switch (translation_mode)
    {
    case NETPROTO_TRANS_CR:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, EOL);
        break;
    case NETPROTO_TRANS_LF:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_LF, EOL);
        break;
    case NETPROTO_TRANS_CRLF:
    #ifndef BUILD_APPLE
        // With Apple2, we would be translating CR to CR; a waste of CPU
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, EOL);
    #endif
        break;
    case NETPROTO_TRANS_PETSCII:
#ifdef VERBOSE_PROTOCOL
        Debug_printf("!!! PETSCII !!!\r\n");
#endif
        *receiveBuffer = mstr::toUTF8(*receiveBuffer);
        break;
    default:
        break;
    }

    if (translation_mode == NETPROTO_TRANS_CRLF)
        receiveBuffer->erase(std::remove(receiveBuffer->begin(), receiveBuffer->end(), '\n'), receiveBuffer->end());
}

/**
 * Perform end of line translation on transmit buffer. based on translation_mode
 * @return new length after translation
 */
unsigned short NetworkProtocol::translate_transmit_buffer()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("#### Translating transmit buffer, mode: %u\r\n", translation_mode);
#endif
    if (translation_mode == 0)
        return transmitBuffer->length();

    #ifdef BUILD_ATARI
    util_replaceAll(*transmitBuffer, STR_ATASCII_BUZZER, STR_ASCII_BELL);
    util_replaceAll(*transmitBuffer, STR_ATASCII_DEL, STR_ASCII_BACKSPACE);
    util_replaceAll(*transmitBuffer, STR_ATASCII_TAB, STR_ASCII_TAB);
    #endif

    switch (translation_mode)
    {
    case NETPROTO_TRANS_CR:
        util_replaceAll(*transmitBuffer, STR_EOL, STR_ASCII_CR);
        break;
    case NETPROTO_TRANS_LF:
        util_replaceAll(*transmitBuffer, STR_EOL, STR_ASCII_LF);
        break;
    case NETPROTO_TRANS_CRLF:
        util_replaceAll(*transmitBuffer, STR_EOL, STR_ASCII_CRLF);
        break;
    case NETPROTO_TRANS_PETSCII:
        *transmitBuffer = mstr::toUTF8(*transmitBuffer);
        break;
    default:
        break;
    }

    return transmitBuffer->length();
}

/**
 * Map errno to error
 */
void NetworkProtocol::errno_to_error()
{
    int err = compat_getsockerr();
    switch (err)
    {
#if defined(_WIN32)
    case WSAEWOULDBLOCK:
        error = 1; // This is okay.
        compat_setsockerr(0); // Short circuit and say it's okay.
    case WSAEADDRINUSE:
        error = NETWORK_ERROR_ADDRESS_IN_USE;
        break;
    case WSAEINPROGRESS:
    case WSAEALREADY:
        error = NETWORK_ERROR_CONNECTION_ALREADY_IN_PROGRESS;
        break;
    case WSAECONNRESET:
        error = NETWORK_ERROR_CONNECTION_RESET;
        break;
    case WSAECONNREFUSED:
        error = NETWORK_ERROR_CONNECTION_REFUSED;
        break;
    case WSAENETUNREACH:
        error = NETWORK_ERROR_NETWORK_UNREACHABLE;
        break;
    case WSAETIMEDOUT:
        error = NETWORK_ERROR_SOCKET_TIMEOUT;
        break;
    case WSAENETDOWN:
        error = NETWORK_ERROR_NETWORK_DOWN;
        break;
#else
    case EAGAIN:
        error = 1; // This is okay.
        compat_setsockerr(0); // Short circuit and say it's okay.
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
#endif
    default:
#ifdef VERBOSE_PROTOCOL
        Debug_printf("errno_to_error() - Uncaught errno = %u, returning 144.\r\n", err);
#endif
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

size_t NetworkProtocol::available()
{
    return receiveBuffer->size();
}

size_t NetworkProtocol::available()
{
    return receiveBuffer->size();
}
