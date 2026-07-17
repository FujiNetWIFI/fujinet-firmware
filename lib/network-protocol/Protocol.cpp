/**
 * Network Protocol Base class
 */

#include "Protocol.h"
#include "TCP.h"

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
 * End-of-line translation model
 * =============================
 * Each target platform has a "native" EOL that its software (including
 * BASIC) expects, and the network side uses one of CR, LF or CR/LF as
 * selected by the translation mode (aux2):
 *
 *   mode 0  binary, no translation
 *   mode 1  network side uses CR   (0x0D)
 *   mode 2  network side uses LF   (0x0A)
 *   mode 3  network side uses CR/LF
 *   mode 4  PETSCII <-> UTF-8 (Commodore, orthogonal to EOL)
 *
 * On receive (FujiNet -> computer) the network line ending is folded into
 * the native EOL; on transmit (computer -> FujiNet) the native EOL is
 * expanded back out to the selected network line ending. The Apple II
 * intentionally deviates from this - see the per-platform branches in
 * translate_receive_buffer().
 */

// Platforms whose native EOL is a CR/LF pair (serial / CP/M-style machines).
#if defined(BUILD_RS232) || defined(BUILD_S100) || defined(BUILD_RC2014) || defined(BUILD_H89)
#define NATIVE_EOL_CRLF
#endif

// Native EOL as a single character (single-EOL platforms only). Apple and the
// CR/LF platforms handle their own EOL in translate_receive_buffer() and do not
// use this value.
#if defined(BUILD_ATARI)
#define NATIVE_EOL_CHAR ATASCII_EOL
#else
#define NATIVE_EOL_CHAR ASCII_CR
#endif

// Native EOL as a string, used by the (uniform) transmit translation.
#if defined(BUILD_ATARI)
#define NATIVE_EOL_STR STR_ATASCII_EOL
#elif defined(NATIVE_EOL_CRLF)
#define NATIVE_EOL_STR STR_ASCII_CRLF
#else
#define NATIVE_EOL_STR STR_ASCII_CR
#endif

/**
 * @brief The line ending a translation mode represents on the network side.
 * @param mode translation mode (aux2)
 * @return CR, LF or CR/LF; empty string for non-EOL modes.
 */
static const char *network_line_ending(netProtoTranslation_t mode)
{
    switch (mode)
    {
    case NETPROTO_TRANS_CR:   return STR_ASCII_CR;
    case NETPROTO_TRANS_LF:   return STR_ASCII_LF;
    case NETPROTO_TRANS_CRLF: return STR_ASCII_CRLF;
    default:                  return "";
    }
}

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
    error = NDEV_STATUS::SUCCESS;
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
 */
fujiError_t NetworkProtocol::open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                                      netProtoTranslation_t translate)
{
    // Set translation mode, Bits 0-1 of aux2
    translation_mode = translate;

    opened_url = urlParser;

    return FUJI_ERROR::NONE;
}

/**
 * @brief Close connection to the protocol.
 */
fujiError_t NetworkProtocol::close()
{
    if (!transmitBuffer->empty())
        write(transmitBuffer->length());

    receiveBuffer->clear();
    transmitBuffer->clear();
    specialBuffer->clear();
    receiveBuffer->shrink_to_fit();
    transmitBuffer->shrink_to_fit();
    specialBuffer->shrink_to_fit();

    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

/**
 * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
 * @param len Number of bytes to read.
 * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
 */
fujiError_t NetworkProtocol::read(unsigned short len)
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("NetworkProtocol::read(%u)\r\n", len);
#endif
    translate_receive_buffer();
    error = NDEV_STATUS::SUCCESS;
    return FUJI_ERROR::NONE;
}

/**
 * @brief Return protocol status information in provided NetworkStatus object.
 * @param status a pointer to a NetworkStatus object to receive status information
 * @param rx_buf a pointer to the receive buffer (to call read())
 * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
 */
fujiError_t NetworkProtocol::status(NetworkStatus *status)
{
    bool isTCP = dynamic_cast<NetworkProtocolTCP*>(this) != nullptr;

    if (fromInterrupt && !isTCP)
        return FUJI_ERROR::NONE;

    if (!was_write && receiveBuffer->length() == 0 && available() > 0)
        read(available());

    return FUJI_ERROR::NONE;
}

/**
 * Perform end of line translation on receiveBuffer (FujiNet -> computer),
 * based on translation_mode. See the translation model note above.
 */
void NetworkProtocol::translate_receive_buffer()
{
#ifdef VERBOSE_PROTOCOL
    Debug_printf("#### Translating receive buffer, mode: %u\r\n", translation_mode);
#endif
    if (translation_mode == NETPROTO_TRANS_NONE)
        return;

    if (translation_mode == NETPROTO_TRANS_PETSCII)
    {
        *receiveBuffer = mstr::toUTF8(*receiveBuffer);
        return;
    }

#ifdef BUILD_ATARI
    // Substitute ATASCII control codes for their ASCII equivalents.
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BELL, ATASCII_BUZZER);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_BACKSPACE, ATASCII_DEL);
    replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_TAB, ATASCII_TAB);
#endif

#if defined(BUILD_APPLE)
    // Apple II native EOL is CR. Modes 1 and 2 leave CR untouched; only mode 3
    // acts, collapsing an incoming CR/LF pair down to a single CR.
    if (translation_mode == NETPROTO_TRANS_CRLF)
        util_replaceAll(*receiveBuffer, STR_ASCII_CRLF, STR_ASCII_CR);

#elif defined(NATIVE_EOL_CRLF)
    // Native EOL is CR/LF; fold the incoming line ending into a CR/LF pair.
    util_replaceAll(*receiveBuffer, network_line_ending(translation_mode), STR_ASCII_CRLF);

#else
    // General case (Atari, Adam, CoCo, and other single-character-EOL
    // platforms): translate the incoming network line ending into the native EOL.
    switch (translation_mode)
    {
    case NETPROTO_TRANS_CR:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, NATIVE_EOL_CHAR);
        break;
    case NETPROTO_TRANS_LF:
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_LF, NATIVE_EOL_CHAR);
        break;
    case NETPROTO_TRANS_CRLF:
        // Map CR to the native EOL and discard the LF half of the pair.
        replace(receiveBuffer->begin(), receiveBuffer->end(), ASCII_CR, NATIVE_EOL_CHAR);
        receiveBuffer->erase(std::remove(receiveBuffer->begin(), receiveBuffer->end(), ASCII_LF), receiveBuffer->end());
        break;
    default:
        break;
    }
#endif
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
    if (translation_mode == NETPROTO_TRANS_NONE)
        return transmitBuffer->length();

    if (translation_mode == NETPROTO_TRANS_PETSCII)
    {
        *transmitBuffer = mstr::toUTF8(*transmitBuffer);
        return transmitBuffer->length();
    }

#ifdef BUILD_ATARI
    // Substitute ASCII control codes for their ATASCII equivalents.
    util_replaceAll(*transmitBuffer, STR_ATASCII_BUZZER, STR_ASCII_BELL);
    util_replaceAll(*transmitBuffer, STR_ATASCII_DEL, STR_ASCII_BACKSPACE);
    util_replaceAll(*transmitBuffer, STR_ATASCII_TAB, STR_ASCII_TAB);
#endif

    // Expand the platform's native EOL out to the selected network line ending.
    // This is uniform across platforms: Atari 0x9B, RS232 CR/LF, others CR.
    util_replaceAll(*transmitBuffer, NATIVE_EOL_STR, network_line_ending(translation_mode));

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
        error = NDEV_STATUS::SUCCESS; // This is okay.
        compat_setsockerr(0); // Short circuit and say it's okay.
    case WSAEADDRINUSE:
        error = NDEV_STATUS::ADDRESS_IN_USE;
        break;
    case WSAEINPROGRESS:
    case WSAEALREADY:
        error = NDEV_STATUS::CONNECTION_ALREADY_IN_PROGRESS;
        break;
    case WSAECONNRESET:
        error = NDEV_STATUS::CONNECTION_RESET;
        break;
    case WSAECONNREFUSED:
        error = NDEV_STATUS::CONNECTION_REFUSED;
        break;
    case WSAENETUNREACH:
        error = NDEV_STATUS::NETWORK_UNREACHABLE;
        break;
    case WSAETIMEDOUT:
        error = NDEV_STATUS::SOCKET_TIMEOUT;
        break;
    case WSAENETDOWN:
        error = NDEV_STATUS::NETWORK_DOWN;
        break;
#else
    case EAGAIN:
        error = NDEV_STATUS::SUCCESS; // This is okay.
        compat_setsockerr(0); // Short circuit and say it's okay.
        break;
    case EADDRINUSE:
        error = NDEV_STATUS::ADDRESS_IN_USE;
        break;
    case EINPROGRESS:
        error = NDEV_STATUS::CONNECTION_ALREADY_IN_PROGRESS;
        break;
    case ECONNRESET:
        error = NDEV_STATUS::CONNECTION_RESET;
        break;
    case ECONNREFUSED:
        error = NDEV_STATUS::CONNECTION_REFUSED;
        break;
    case ENETUNREACH:
        error = NDEV_STATUS::NETWORK_UNREACHABLE;
        break;
    case ETIMEDOUT:
        error = NDEV_STATUS::SOCKET_TIMEOUT;
        break;
    case ENETDOWN:
        error = NDEV_STATUS::NETWORK_DOWN;
        break;
#endif
    default:
#ifdef VERBOSE_PROTOCOL
        Debug_printf("errno_to_error() - Uncaught errno = %u, returning 144.\r\n", err);
#endif
        error = NDEV_STATUS::GENERAL;
        break;
    }
}

size_t NetworkProtocol::available()
{
    return receiveBuffer->size();
}
