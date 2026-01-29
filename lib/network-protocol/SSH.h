/**
 * SSH Protocol Adapter
 */

#ifndef NETWORKPROTOCOL_SSH
#define NETWORKPROTOCOL_SSH

#ifdef ESP_PLATFORM
#include <lwip/sockets.h>
#endif

#include <string>

#include "Protocol.h"

#include "fnTcpClient.h"
#include <libssh/libssh.h>
#ifdef ESP_PLATFORM
// apc: this is libssh private header!
#include "libssh/session.h"
#endif

// using namespace std;

class NetworkProtocolSSH : public NetworkProtocol
{

public:
    /**
     * ctor
     */
    NetworkProtocolSSH(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolSSH();

    /**
     * @brief Open connection to the protocol using URL
     * @param urlParser The URL object passed in to open.
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close connection to the protocol.
     */
    protocolError_t close() override;

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
     * @brief Return protocol status information in provided NetworkStatus object.
     * @param status a pointer to a NetworkStatus object to receive status information
     * @return PROTOCOL_ERROR::NONE on success, PROTOCOL_ERROR::UNSPECIFIED on error
     */
    protocolError_t status(NetworkStatus *status) override;

private:
    /**
     * The libssh session structure
     */
    ssh_session session;

    /**
     * The libssh communication channel
     */
    ssh_channel channel;

    /**
     * The underlying TCP client
     */
    fnTcpClient client;

    /**
     * Host Key Fingerprint
     */
    unsigned char *fingerprint = nullptr;

    /**
     * User Auth list
     */
    const char *userauthlist = nullptr;

    /**
     * Intermediate RX buffer
     */
    char *rxbuf = nullptr;

    /**
     * Return if bytes available by injecting into RX buffer.
     * @return number of bytes available
     */
    size_t available() override;
};

#endif /* NETWORKPROTOCOL_SSH */
