/**
 * SSH.COPYID Protocol Adapter
 *
 * Installs the FujiNet default SSH public key on a remote server,
 * similar to the Unix ssh-copy-id command.
 *
 * Usage:
 *   N:SSH.COPYID://user:pass@host:port/
 *
 * Reads:
 *   /.ssh/id_ed25519.pub  (public key in authorized_keys format)
 *
 * Remote effect:
 *   Appends the public key to ~/.ssh/authorized_keys on the remote
 *   host (creating the directory/file if needed), skipping the
 *   append if the key is already present.
 *
 * Returns a textual status response that can be read back by the
 * Atari client.
 */

#ifndef NETWORKPROTOCOL_SSHCOPYID
#define NETWORKPROTOCOL_SSHCOPYID

#include <string>

#include "Protocol.h"

#include <libssh/libssh.h>

class NetworkProtocolSSHCopyId : public NetworkProtocol
{

public:
    /**
     * ctor
     */
    NetworkProtocolSSHCopyId(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolSSHCopyId();

    /**
     * @brief Open — parse URL, connect, install public key, populate status response.
     * @param urlParser The URL object passed in to open.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close — no-op (session cleaned up in open).
     */
    fujiError_t close() override { return FUJI_ERROR::NONE; }

    /**
     * @brief Read status response.
     * @param len Number of bytes to read.
     * @return FUJI_ERROR::NONE on success
     */
    fujiError_t read(unsigned short len) override;

    /**
     * @brief Write — not supported for SSH.COPYID.
     */
    fujiError_t write(unsigned short len) override;

    /**
     * @brief Return protocol status information.
     */
    fujiError_t status(NetworkStatus *status) override;

    /**
     * @brief Return available bytes in status response.
     */
    size_t available() override { return statusResponse.length(); }

private:
    /**
     * Textual status response returned to the Atari client on read().
     */
    std::string statusResponse;

    /**
     * @brief Get absolute filesystem path to the public key on SD card.
     */
    std::string getPublicKeyPath();

    /**
     * @brief Read the default public key from SD card.
     * @param[out] pubkeyLine The public key line (trimmed, no trailing newline).
     * @return true on success
     */
    bool readDefaultPublicKey(std::string &pubkeyLine);

    /**
     * @brief Validate that a string looks like an Ed25519 authorized_keys entry.
     * @param line The public key line to validate.
     * @return true if valid
     */
    bool validateEd25519PublicKey(const std::string &line);

    /**
     * @brief Connect to remote host with password authentication.
     * @param host Remote hostname.
     * @param port Remote port.
     * @param user SSH username.
     * @param pass SSH password.
     * @param[out] sess The created ssh_session (caller must free on success).
     * @return true on success
     */
    bool connectWithPassword(const std::string &host, int port,
                             const std::string &user, const std::string &pass,
                             ssh_session &sess);

    /**
     * @brief Execute the remote install command and read back result.
     * @param sess An authenticated ssh_session.
     * @param pubkeyLine The public key line to install.
     * @param[out] installed true if key was newly installed, false if already present.
     * @return true on success
     */
    bool installPublicKey(ssh_session sess, const std::string &pubkeyLine,
                          bool &installed);

    /**
     * @brief Build the remote shell command that installs the public key.
     * @param pubkeyLine The public key line.
     * @return The shell command string.
     */
    std::string buildRemoteInstallCommand(const std::string &pubkeyLine);

    /**
     * @brief Shell-quote a string using single quotes.
     * @param s The string to quote.
     * @return The safely quoted string.
     */
    static std::string shellQuoteSingle(const std::string &s);

    /**
     * @brief Set success status response.
     */
    void setStatusResponse(const std::string &host, const std::string &user,
                           bool installed);

    /**
     * @brief Set error status response.
     */
    void setErrorResponse(const std::string &msg);
};

#endif /* NETWORKPROTOCOL_SSHCOPYID */
