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
     * @param cmdFrame The command frame to extract aux1/aux2/etc.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t open(PeoplesUrlParser *urlParser, cmdFrame_t *cmdFrame) override;

    /**
     * @brief Close connection to the protocol.
     */
    netProtoErr_t close() override;

    /**
     * @brief Read len bytes into rx_buf, If protocol times out, the buffer should be null padded to length.
     * @param len Number of bytes to read.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t read(unsigned short len) override;

    /**
     * @brief Write len bytes from tx_buf to protocol.
     * @param len The # of bytes to transmit, len should not be larger than buffer.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t status(NetworkStatus *status) override;

    /**
     * @brief Return a DSTATS byte for a requested COMMAND byte.
     * @param cmd The Command (0x00-0xFF) for which DSTATS is requested.
     * @return a 0x00 = No payload, 0x40 = Payload to Atari, 0x80 = Payload to FujiNet, 0xFF = Command not supported.
     */
    AtariSIODirection special_inquiry(fujiCommandID_t cmd) override;

    /**
     * @brief execute a command that returns no payload
     * @param cmdFrame a pointer to the passed in command frame for aux1/aux2/etc
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_00(cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that returns a payload to the atari.
     * @param sp_buf a pointer to the special buffer
     * @param len Length of data to request from protocol. Should not be larger than buffer.
     * @return NETPROTO_ERR_NONE on success, NETPROTO_ERR_UNSPECIFIED on error
     */
    netProtoErr_t special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    /**
     * @brief execute a command that sends a payload to fujinet (most common, XIO)
     * @param sp_buf, a pointer to the special buffer, usually a EOL terminated devicespec.
     * @param len length of the special buffer, typically SPECIAL_BUFFER_SIZE
     */
    netProtoErr_t special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame) override;

    size_t available() override { return udp.available(); }

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

    /**
     * @brief Set destination address
     * @param sp_buf pointer to received special buffer.
     * @param len of special received buffer
     */
    netProtoErr_t set_destination(uint8_t *sp_buf, unsigned short len);

#ifndef ESP_PLATFORM
    /**
     * @brief Get remote address
     * @param sp_buf pointer to transmit special buffer.
     * @param len of special transmit buffer
     */
    netProtoErr_t get_remote(uint8_t *sp_buf, unsigned short len);
#endif

private:
    /**
     * Is current destination multicast?
     */
    bool is_multicast(); // current UDP remote IP
    bool is_multicast(std::string h); // resolve hostname to IP first.
    bool is_multicast(in_addr_t addr);
};

#endif /* NETWORKPROTOCOL_UDP */
