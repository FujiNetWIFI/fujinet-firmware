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

private:
    /**
     * The libssh session structure.
     * Initialised to nullptr so close() can safely null-check it on
     * teardown paths where open() failed before ssh_new().
     */
    ssh_session session = nullptr;

    /**
     * The libssh communication channel.
     * Initialised to nullptr so close() can safely null-check it on
     * teardown paths where open() failed before ssh_channel_new().
     */
    ssh_channel channel = nullptr;

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
     * @brief Return if bytes available by injecting into RX buffer.
     * @return number of bytes available
     */
    size_t available() override;

    /**
     * @brief Check if URL has a password (user:pass@host vs user@host)
     * @return true if password-based authentication should be used
     */
    bool hasPassword();

    /**
     * @brief Authenticate using password
     * @return true on success
     */
    bool authenticateWithPassword();

    /**
     * @brief Authenticate using default private key from SD card
     * @return true on success
     */
    bool authenticateWithDefaultKey();

    /**
     * @brief Get the filesystem path to the default SSH private key on SD card
     * @return absolute path to /.ssh/id_ed25519 on the SD card
     */
    std::string getDefaultPrivateKeyPath();

    /**
     * @brief True if URL provided a password (password auth), false for key auth
     */
    bool usePasswordAuth = true;
};

#endif /* NETWORKPROTOCOL_SSH */
