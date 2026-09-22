/**
 * Network protocol implementation for UDP sockets
 */

#ifndef NETWORKPROTOCOL_UDP
#define NETWORKPROTOCOL_UDP

#include "Protocol.h"
#include "fnUDP.h"

class NetworkProtocolUDP : public NetworkProtocol
{
public:
    /**
     * ctor
     */
    NetworkProtocolUDP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolUDP();

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close connection to the protocol.
     */
    fujiError_t close() override;

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t status(NetworkStatus *status) override;

#ifndef ESP_PLATFORM
    /**
     * @brief Get remote address
     * @param sp_buf pointer to transmit special buffer.
     * @param len of special transmit buffer
     */
    fujiError_t get_remote(void *sp_buf, unsigned short len);
#endif

    /**
     * @brief Set destination address
     * @param sp_buf pointer to received special buffer.
     * @param len of special received buffer
     */
    fujiError_t set_destination(const uint8_t *sp_buf, unsigned short len);

    /* Mirror NetworkProtocolTCP::available(): the RECEIVE BUFFER first, and
     * the socket only when that is empty.
     *
     * Returning udp.available() alone makes a datagram invisible the moment
     * anything reads it. NetworkProtocol::status() does exactly that -- it
     * drains the socket into receiveBuffer before the caller asks how much is
     * waiting -- so rs232_status_channel()'s `avail = protocol->available()`
     * then reports the empty socket, the client is told nothing arrived, and
     * the bytes sit in the buffer until the next datagram displaces them.
     *
     * TCP has always had the two-line version and every shipping client in
     * this family is TCP or HTTP, which is why this survived: the first UDP
     * client found it, and what it looked like was a server that answered one
     * packet in twenty-five. */
    size_t available() override
    {
        size_t avail = receiveBuffer->size();
        if (!avail)
            avail = udp.available();
        return avail;
    }

protected:

    /**
     * Object representing UDP endpoint
     */
    fnUDP udp;

    /**
     * UDP destination address
     */
    std::string dest;

    /**
     * UDP destination port
     */
    unsigned short port = 0;

    /**
     * Do multicast write?
     */
    bool multicast_write = false;

private:
    /**
     * Is current destination multicast?
     */
    bool is_multicast(); // current UDP remote IP
    bool is_multicast(std::string h); // resolve hostname to IP first.
    bool is_multicast(in_addr_t addr);
};

#endif /* NETWORKPROTOCOL_UDP */
