#ifndef NETWORKPROTOCOL_H
#define NETWORKPROTOCOL_H

#include <vector>
#include "sio.h"
#include "EdUrlParser.h"
#include "networkStatus.h"

class NetworkProtocol
{
public:
    /**
     * dtor
     */
    virtual ~NetworkProtocol() {}

    /**
     * @brief Protocol connection is a server (listening connection)
     */
    bool connectionIsServer = false;

    /**
     * @brief number of bytes waiting
     */
    unsigned short bytesWaiting;

    /**
     * @brief Error code to return in status
     */
    unsigned char error;

    /**
     * Translation mode: 0=NONE, 1=CR, 2=LF, 3=CR/LF
     */
    unsigned char translation_mode;

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @param cmdFrame The command frame to extract aux1/aux2/etc.
     */
    virtual bool open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame);

    /**
     * @brief Close connection to the protocol.
     */
    virtual bool close();

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param rx_buf The destination buffer to accept received bytes from protocol.
     * @param len The # of bytes expected from protocol adapter. Buffer should be large enough.
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool read(uint8_t *rx_buf, unsigned short len) = 0;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param tx_buf The buffer containing data to transmit.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool write(uint8_t *tx_buf, unsigned short len) = 0;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool status(NetworkStatus *status) = 0;

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    virtual uint8_t special_inquiry(uint8_t cmd) = 0;

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return error flag. TRUE on error, FALSE on success.
     */
    virtual bool special_00(cmdFrame_t *cmdFrame) = 0;

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return error flag. TRUE on error, FALSE on success.
     */ 
    virtual bool special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) = 0;

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    virtual bool special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) = 0;

private:

    /**
     * Temporary end of line transform buffer
     */
    vector<char> transformBuffer;

    /**
     * Perform end of line translation on receive buffer.
     * @param rx_buf ptr to The receive buffer to transform
     * @param len The length of the receive buffer
     * @return length after transformation.
     */
    unsigned short translate_receive_buffer(uint8_t *rx_buf, unsigned short len);

    /**
     * Perform end of line translation on transmit buffer.
     * @param tx_buf ptr to The transmit buffer to transform
     * @param len The length of the transmit buffer
     * @return length after transformation.
     */
    unsigned short translate_transmit_buffer(uint8_t *tx_buf, unsigned short len);

    /**
     * Copy char buffer into transform buffer
     * @param buf pointer to the buffer to copy into transform buffer.
     * @param len The length of the source buffer
     */
    void populate_transform_buffer(uint8_t *buf, unsigned short len);

    /**
     * Copy transform buffer back into destination buffer
     * @param buf pointer to destination buffer for the transform buffer
     */
    void copy_transform_buffer(uint8_t *buf);

};

#endif /* NETWORKPROTOCOL_H */
