/**
 * Network Protocol Base class
 */

#include <string.h>
#include "Protocol.h"

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
 * @param rx_len length of receive buffer
 * @param tx_buf pointer to transmit buffer
 * @param tx_len length of receive buffer
 * @param sp_buf pointer to special buffer
 * @param sp_len length of special buffer
 */
NetworkProtocol::NetworkProtocol(uint8_t *rx_buf, uint16_t rx_len,
                                 uint8_t *tx_buf, uint16_t tx_len,
                                 uint8_t *sp_buf, uint16_t sp_len)
{
    receiveBuffer = rx_buf;
    receiveBufferCapacity = rx_len;
    transmitBuffer = tx_buf;
    transmitBufferCapacity = tx_len;
    specialBuffer = sp_buf;
    specialBufferCapacity = sp_len;
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
    return true;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocol::close()
{
    // Clear transform buffer
    transformBuffer.clear();
    return true;
}

/**
 * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
 * @param len Number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::read(unsigned short len)
{
    translate_receive_buffer();
    return false;
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param tx_buf The buffer containing data to transmit.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::write(uint8_t *tx_buf, unsigned short len)
{
    translate_transmit_buffer();
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
    translate_receive_buffer();
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
    // Do not alter buffer if no translation is required
    if (translation_mode == 0)
        return;

    // Copy contents of receive buffer into transform buffer
    populate_transform_buffer(receiveBuffer,receiveBufferSize);

    for (vector<char>::iterator it = transformBuffer.begin(); it != transformBuffer.end(); ++it)
    {
        char c = *it;

        // Convert these ASCII characters to ATASCII in all cases.
        switch (c)
        {
        case ASCII_BELL:
            *it = ATASCII_BUZZER;
            break;
        case ASCII_BACKSPACE:
            *it = ATASCII_DEL;
            break;
        case ASCII_TAB:
            *it = ATASCII_TAB;
            break;
        }

        // Handle EOL translation.
        if ((translation_mode == TRANSLATION_MODE_CR && c == ASCII_CR) ||
            (translation_mode == TRANSLATION_MODE_LF && c == ASCII_LF) ||
            (translation_mode == TRANSLATION_MODE_CRLF && c == ASCII_CR))
            *it = ATASCII_EOL;

        // If CR/LF, then remove the now spurious linefeed.
        if (translation_mode == TRANSLATION_MODE_CRLF && c == ASCII_LF)
            it = transformBuffer.erase(it);
    }

    // Copy transform buffer back into rx buffer.
    copy_transform_buffer(receiveBuffer);
}

/**
 * Perform end of line translation on transmit buffer. based on translation_mode
 */
void NetworkProtocol::translate_transmit_buffer()
{
    // Do not alter buffer if no translation is required.
    if (translation_mode == 0)
        return;

    // Copy contents of transmit buffer into transform buffer
    populate_transform_buffer(transmitBuffer,transmitBufferCapacity);

    for (vector<char>::iterator it = transformBuffer.begin(); it != transformBuffer.end(); ++it)
    {
        char c = *it;

        // Convert these ATASCII characters to ASCII in all cases
        switch (c)
        {
        case ATASCII_BUZZER:
            *it = ASCII_BELL;
            break;
        case ATASCII_DEL:
            *it = ASCII_BACKSPACE;
            break;
        case ATASCII_TAB:
            *it = ASCII_TAB;
            break;
        case ATASCII_EOL:
            if (translation_mode == 1)
                *it = ASCII_CR;
            else if (translation_mode == 2)
                *it = ASCII_LF;
            else if (translation_mode == 3)
            {
                *it = ASCII_CR;
                it = transformBuffer.insert(it, ASCII_LF);
            }
        }
    }
    copy_transform_buffer(transmitBuffer);
}

/**
 * Copy char buffer into transform buffer
 * @param buf pointer to the buffer to copy into transform buffer.
 * @param len The length of the source buffer
 */
void NetworkProtocol::populate_transform_buffer(uint8_t *buf, unsigned short len)
{
    transformBuffer.clear();

    for (int i = 0; i < len; i++)
        transformBuffer.push_back(buf[i]);

    Debug_printf("NetworkProtocol::populate_transform_buffer(%p,%u)\n", buf, len);
}

/**
 * Copy transform buffer back into destination buffer
 * @param buf pointer to destination buffer for the transform buffer
 */
void NetworkProtocol::copy_transform_buffer(uint8_t *buf)
{
    memset(buf, 0, transformBuffer.size());
    memcpy(buf, transformBuffer.data(), transformBuffer.size());
}
