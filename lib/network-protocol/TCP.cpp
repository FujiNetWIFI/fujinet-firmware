/**
 * NetworkProtocolTCP
 * 
 * TCP Protocol Adapter Implementation
 */

#include "TCP.h"

/**
 * @brief Open connection to the protocol using URL
 * @param urlParser The URL object passed in to open.
 * @param cmdFrame The command frame to extract aux1/aux2/etc.
 */
bool NetworkProtocolTCP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    return false;
}

/**
 * @brief Close connection to the protocol.
 */
bool NetworkProtocolTCP::close()
{
    return false;
}

/**
 * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
 * @param rx_buf The destination buffer to accept received bytes from protocol.
 * @param len The # of bytes expected from protocol adapter. Buffer should be large enough.
 * @return error flag. FALSE if successful, TRUE if error.
 */
bool NetworkProtocolTCP::read(uint8_t *rx_buf, unsigned short len)
{
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