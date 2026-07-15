/**
 * SSH.KEYGEN Protocol Adapter
 *
 * Generates an SSH key pair on the FujiNet SD card.
 *
 * Usage:
 *   N:SSH.KEYGEN://ed25519/             — generate (fail if key exists)
 *   N:SSH.KEYGEN://ed25519/?overwrite=1 — generate (replace existing key)
 *
 * Creates:
 *   /.ssh/id_ed25519      (private key, OpenSSH format)
 *   /.ssh/id_ed25519.pub  (public key, authorized_keys format)
 *
 * Returns a textual status response that can be read back by the
 * Atari client.
 */

#ifndef NETWORKPROTOCOL_SSHKEYGEN
#define NETWORKPROTOCOL_SSHKEYGEN

#include <string>

#include "Protocol.h"

class NetworkProtocolSSHKeygen : public NetworkProtocol
{

public:
    /**
     * ctor
     */
    NetworkProtocolSSHKeygen(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf);

    /**
     * dtor
     */
    virtual ~NetworkProtocolSSHKeygen();

    /**
     * @brief Open connection — parses URL, generates key pair, populates status response.
     * @param urlParser The URL object passed in to open.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t open(PeoplesUrlParser *urlParser, fileAccessMode_t access,
                         netProtoTranslation_t translate) override;

    /**
     * @brief Close — no-op.
     */
    fujiError_t close() override { return FUJI_ERROR::NONE; }

    /**
     * @brief Read status response.
     * @param len Number of bytes to read.
     * @return FUJI_ERROR::NONE on success, FUJI_ERROR::UNSPECIFIED on error
     */
    fujiError_t read(unsigned short len) override;

    /**
     * @brief Write — not supported for keygen.
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
     * @brief Get absolute filesystem path to the SSH directory on SD card.
     */
    std::string getSshDirPath();

    /**
     * @brief Get absolute filesystem path to the private key.
     */
    std::string getPrivateKeyPath();

    /**
     * @brief Get absolute filesystem path to the public key.
     */
    std::string getPublicKeyPath();

    /**
     * @brief Ensure /.ssh directory exists on SD card; create if missing.
     * @return true on success
     */
    bool ensureSshDirectoryExists();

    /**
     * @brief Check if either key file already exists.
     * @return true if at least one key file exists
     */
    bool keyFilesAlreadyExist();

    /**
     * @brief Generate an Ed25519 key pair and save to SD card.
     * @param useTempFiles If true, write to .tmp files first then rename.
     * @return true on success
     */
    bool generateEd25519KeyPair(bool useTempFiles);

    /**
     * @brief Write public key in authorized_keys one-line format with comment.
     * @param key The generated key
     * @param path Absolute filesystem path
     * @return true on success
     */
    bool writePublicKey(void *key, const std::string &path);

    /**
     * @brief Parse query string for overwrite=1 parameter.
     * @param query Raw query string from URL
     * @return true only if query contains exactly "overwrite=1"
     */
    bool parseOverwrite(const std::string &query);

    /**
     * @brief Remove a file, ignoring errors if it doesn't exist.
     */
    void removeFile(const std::string &path);
};

#endif /* NETWORKPROTOCOL_SSHKEYGEN */
