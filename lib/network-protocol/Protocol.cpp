/**
 * Network Protocol Base class
 */

#include <string.h>
#include <algorithm>
#include "Protocol.h"

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
    Debug_printf("NetworkProtocol::ctor()\n");

    if (rx_buf == nullptr)
    {
        Debug_printf("rx_buf is NULL\n");
    }

    if (tx_buf == nullptr)
    {
        Debug_printf("tx_buf is NULL\n");
    }

    if (sp_buf == nullptr)
    {
        Debug_printf("sp_buf is NULL\n");
    }

    receiveBuffer = rx_buf;
    receiveBufferCapacity = rx_len;
    transmitBuffer = tx_buf;
    transmitBufferCapacity = tx_len;
    specialBuffer = sp_buf;
    specialBufferCapacity = sp_len;

    Debug_printf("Buffers: %p (%u) %p (%u) %p (%u)\n", receiveBuffer, rx_len, transmitBuffer, tx_len, specialBuffer, sp_len);
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
    return false;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocol::close()
{
    // Clear transform buffer
    transformBuffer.clear();
    return false;
}

/**
 * @brief Read len bytes into receiveBuffer, If protocol times out, the buffer should be null padded to length.
 * @param len Number of bytes to read.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::read(unsigned short len)
{
    Debug_printf("NetworkProtocol::read(%u)\n",len);
    translate_receive_buffer();
    receiveBufferSize -= len;
    return false;
}

/**
 * @brief Write len bytes from tx_buf to protocol.
 * @param len The # of bytes to transmit, len should not be larger than buffer.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocol::write(unsigned short len)
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
    if (receiveBufferSize == 0 && status->rxBytesWaiting > 0)
        read(status->rxBytesWaiting);

    status->rxBytesWaiting = receiveBufferSize;

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

    populate_transform_buffer(receiveBuffer, receiveBufferSize);
    memset(receiveBuffer,0,receiveBufferCapacity);

    Debug_printf("Transform buffer size before: %u",transformBuffer.size());

    replace(transformBuffer.begin(), transformBuffer.end(), ASCII_BELL, ATASCII_BUZZER);
    replace(transformBuffer.begin(), transformBuffer.end(), ASCII_BACKSPACE, ATASCII_DEL);
    replace(transformBuffer.begin(), transformBuffer.end(), ASCII_TAB, ATASCII_TAB);

    switch (translation_mode)
    {
    case TRANSLATION_MODE_CR:
        replace(transformBuffer.begin(), transformBuffer.end(), ASCII_CR, ATASCII_EOL);
        break;
    case TRANSLATION_MODE_LF:
        replace(transformBuffer.begin(), transformBuffer.end(), ASCII_LF, ATASCII_EOL);
        break;
    case TRANSLATION_MODE_CRLF:
        replace(transformBuffer.begin(), transformBuffer.end(), ASCII_CR, ATASCII_EOL);
        break;
    }

    if (translation_mode == TRANSLATION_MODE_CRLF)
        transformBuffer.erase(remove(transformBuffer.begin(), transformBuffer.end(), '\n'), transformBuffer.end());

    receiveBufferSize = transformBuffer.size();
    Debug_printf("Transform buffer size after: %u",transformBuffer.size());

    copy_transform_buffer(receiveBuffer);
}

/**
 * Perform end of line translation on transmit buffer. based on translation_mode
 */
void NetworkProtocol::translate_transmit_buffer()
{
    if (translation_mode == 0)
        return;

    replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_BUZZER, ASCII_BELL);
    replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_DEL, ASCII_BACKSPACE);
    replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_TAB, ASCII_TAB);

    switch (translation_mode)
    {
    case TRANSLATION_MODE_CR:
        replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_EOL, ASCII_CR);
        break;
    case TRANSLATION_MODE_LF:
        replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_EOL, ASCII_LF);
        break;
    case TRANSLATION_MODE_CRLF:
        replace(transformBuffer.begin(), transformBuffer.end(), ATASCII_EOL, ASCII_CR);
        break;
    }

    // If CR/LF, insert linefeed wherever there is a CR.
    if (translation_mode == TRANSLATION_MODE_CRLF)
    {
        auto pos = transformBuffer.find(ASCII_CR);
        while (pos != string::npos)
        {
            pos++;
            transformBuffer.insert(pos, "\n");
            pos = transformBuffer.find(ASCII_CR);
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
    transformBuffer = string((char *)buf, len);
}

/**
 * Copy transform buffer back into destination buffer
 * @param buf pointer to destination buffer for the transform buffer
 */
void NetworkProtocol::copy_transform_buffer(uint8_t *buf)
{
    memcpy(buf, transformBuffer.data(), transformBuffer.size());

    Debug_printf("copy_transform_buffer() - ");
    for (int i = 0; i < transformBuffer.size(); i++)
        Debug_printf("%02x ", buf[i]);
    Debug_printf("\n");
}
