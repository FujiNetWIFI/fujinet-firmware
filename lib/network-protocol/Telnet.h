/**
 * Network Protocol implementation for TELNET sockets
 */

#ifndef NETWORKPROTOCOL_TELNET
#define NETWORKPROTOCOL_TELNET

#include "TCP.h"


class NetworkProtocolTELNET : public NetworkProtocolTCP
{
public:
    /**
     * ctor
     */
    NetworkProtocolTELNET(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolTELNET();

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t write(unsigned short len) override;

    /**
     * Get Receive Buffer
     */
    std::string *getReceiveBuffer() { return receiveBuffer; }

    /**
     * Get Transmit buffer
     */
    std::string *getTransmitBuffer() { return transmitBuffer; }

    /**
     * Flush output transmitBuffer
     */
    void flush(const char* buf, unsigned short size);

    /**
     * Length after RX processing
     */
    int newRxLen;

    char ttype[32]="dumb";
};

#endif /* NETWORKPROTOCOL_TELNET */
