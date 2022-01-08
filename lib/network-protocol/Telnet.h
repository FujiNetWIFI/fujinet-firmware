/**
 * Network Protocol implementation for TELNET sockets
 */

#ifndef NETWORKPROTOCOL_TELNET
#define NETWORKPROTOCOL_TELNET

#include "TCP.h"
#include "../tcpip/fnTcpClient.h"
#include "../tcpip/fnTcpServer.h"

class NetworkProtocolTELNET : public NetworkProtocolTCP
{
public:
    /**
     * ctor
     */
    NetworkProtocolTELNET(string *rx_buf, string *tx_buf, string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolTELNET();

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool read(unsigned short len);

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return error flag. FALSE if successful, TRUE if error.
     */
    virtual bool write(unsigned short len);

    /**
     * Get Receive Buffer
     */
    string *getReceiveBuffer() { return receiveBuffer; }

    /**
     * Get Transmit buffer
     */
    string *getTransmitBuffer() { return transmitBuffer; }

    /**
     * Flush output transmitBuffer
     */
    void flush(const char* buf, unsigned short size);

    /**
     * Length after RX processing
     */
    int newRxLen;

    char ttype[32]="heath-19";
    
};

#endif /* NETWORKPROTOCOL_TELNET */