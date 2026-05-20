/**
 * SSH protocol implementation
 *
 * Supports two authentication modes, inferred from the URL:
 *
 *   N:SSH://user:pass@host:port/   => password authentication
 *   N:SSH://user@host:port/        => public-key authentication
 *                                     using default key from SD card:
 *                                     /.ssh/id_ed25519
 *
 * The auth mode is determined solely by whether the URL contains a
 * password component.  No query parameters are needed.
 */

#include "SSH.h"

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <vector>

#define RXBUF_SIZE 65535

/**
 * Default SSH private key path relative to SD card root.
 */
#define SSH_DEFAULT_KEY_REL "/.ssh/id_ed25519"

/**
 * SD card base path differs between ESP and PC builds.
 */
#ifdef ESP_PLATFORM
#define SD_BASE_PATH "/sd"
#else
#define SD_BASE_PATH "SD"
#endif

NetworkProtocolSSH::NetworkProtocolSSH(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolSSH::NetworkProtocolSSH(%p,%p,%p)\r\n", rx_buf, tx_buf, sp_buf);
#ifdef ESP_PLATFORM
    rxbuf = (char *)heap_caps_malloc(RXBUF_SIZE, MALLOC_CAP_SPIRAM);
#else
    rxbuf = (char *)malloc(RXBUF_SIZE);
#endif
    if (rxbuf == nullptr)
    {
        /*
         * Fix (2026-05-19): heap_caps_malloc(MALLOC_CAP_SPIRAM) can
         * fail when PSRAM is unavailable or exhausted.  Previously
         * the NULL pointer was used unguarded in ssh_channel_read()
         * and std::string::append() below, which crashed the firmware.
         * The destructor now also tolerates a NULL rxbuf, but log the
         * allocation failure so the operator can see it.
         */
        Debug_printf("NetworkProtocolSSH::NetworkProtocolSSH() - rxbuf allocation FAILED (%u bytes)\r\n",
                     (unsigned)RXBUF_SIZE);
    }
}

NetworkProtocolSSH::~NetworkProtocolSSH()
{
    Debug_printf("NetworkProtocolSSH::~NetworkProtocolSSH()\r\n");
    /*
     * Fix (2026-05-19): guard against a NULL rxbuf so a failed
     * heap_caps_malloc() in the ctor doesn't crash the dtor.  Null
     * the pointer after the free so a (hypothetical) second dtor
     * invocation would not double-free.
     */
    if (rxbuf != nullptr)
    {
#ifdef ESP_PLATFORM
        heap_caps_free(rxbuf);
#else
        free(rxbuf);
#endif
        rxbuf = nullptr;
    }
}

/* ------------------------------------------------------------------ */
/* Helper: check if URL provided a password                           */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSH::hasPassword()
{
    return password != nullptr && !password->empty();
}

/* ------------------------------------------------------------------ */
/* Helper: build absolute filesystem path to default private key      */
/* ------------------------------------------------------------------ */
std::string NetworkProtocolSSH::getDefaultPrivateKeyPath()
{
    return std::string(SD_BASE_PATH) + SSH_DEFAULT_KEY_REL;
}

/* ------------------------------------------------------------------ */
/* Helper: authenticate with password                                 */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSH::authenticateWithPassword()
{
    Debug_printf("SSH auth mode: password\r\n");

    int ret = ssh_userauth_list(session, NULL);
    bool allowsPassword = ret & SSH_AUTH_METHOD_PASSWORD;

    if (!allowsPassword) {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSSH::authenticateWithPassword() - Server does not allow password auth.\r\n");
        return false;
    }

    ret = ssh_userauth_password(session, NULL, password->c_str());
    if (ret != SSH_AUTH_SUCCESS) {
        error = NDEV_STATUS::ACCESS_DENIED;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::authenticateWithPassword() - Password auth failed, error: %s.\r\n", message);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Helper: authenticate with default private key from SD card         */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSH::authenticateWithDefaultKey()
{
    Debug_printf("SSH auth mode: publickey\r\n");

    int ret = ssh_userauth_list(session, NULL);
    bool allowsPublicKey = ret & SSH_AUTH_METHOD_PUBLICKEY;

    if (!allowsPublicKey) {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSSH::authenticateWithDefaultKey() - Server does not allow public key auth.\r\n");
        return false;
    }

    std::string keyPath = getDefaultPrivateKeyPath();
    Debug_printf("SSH private key: %s\r\n", keyPath.c_str());

    /* Import the private key from file */
    ssh_key privkey = NULL;
    ret = ssh_pki_import_privkey_file(keyPath.c_str(), NULL, NULL, NULL, &privkey);
    if (ret != SSH_OK) {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSSH::authenticateWithDefaultKey() - "
                     "Could not load private key from %s (error code: %d). "
                     "Check that the file exists and is a valid SSH private key.\r\n",
                     keyPath.c_str(), ret);
        return false;
    }

    /* Authenticate with the imported key */
    ret = ssh_userauth_publickey(session, NULL, privkey);
    ssh_key_free(privkey);

    if (ret != SSH_AUTH_SUCCESS) {
        error = NDEV_STATUS::ACCESS_DENIED;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::authenticateWithDefaultKey() - "
                     "Public key auth failed, error: %s.\r\n", message);
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* open() — main connection and authentication flow                   */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSH::open(PeoplesUrlParser *urlParser,
                                         fileAccessMode_t access,
                                         netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);
    int ret;

    /* ---- Parse credentials from URL ---- */
    if (!urlParser->user.empty()) {
        login = &urlParser->user;
    }

    if (!urlParser->password.empty()) {
        password = &urlParser->password;
    }

    /* Determine auth mode: password if URL has password, key otherwise */
    usePasswordAuth = hasPassword();

    /* Username is always required */
    if (!login || login->empty()) {
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        Debug_printf("NetworkProtocolSSH::open() - Missing SSH username.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* For password auth, password must be present */
    if (usePasswordAuth && (!password || password->empty())) {
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        Debug_printf("NetworkProtocolSSH::open() - Password auth selected but password is empty.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Port 22 by default.
    if (urlParser->port.empty())
    {
        urlParser->port = "22";
    }

    if ((ret = ssh_init()) != 0)
    {
        Debug_printf("NetworkProtocolSSH::open() - ssh_init not successful. Value returned: %d\r\n", ret);
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    Debug_printf("NetworkProtocolSSH::open() - Opening session.\r\n");
    session = ssh_new();
    if (session == NULL)
    {
        Debug_printf("Could not create session. aborting.\r\n");
        error = NDEV_STATUS::NOT_CONNECTED;
        return FUJI_ERROR::UNSPECIFIED;
    }

    int verbosity = SSH_LOG_PROTOCOL;
    int port = urlParser->getPort();
    ssh_options_set(session, SSH_OPTIONS_USER, login->c_str());
    ssh_options_set(session, SSH_OPTIONS_HOST, urlParser->host.c_str());
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
#ifdef ESP_PLATFORM // apc: access to private member!
    session->opts.config_processed = true;
#endif

    ret = ssh_connect(session);
    if (ret != SSH_OK)
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not connect, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Host key verification ---- */
    ssh_key srv_pubkey = NULL;
    ret = ssh_get_server_publickey(session, &srv_pubkey);
    if (ret < 0) {
        error = NDEV_STATUS::GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not get server ssh public key, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }

    size_t hlen;
    ret = ssh_get_publickey_hash(srv_pubkey,
                                SSH_PUBLICKEY_HASH_SHA1,
                                &fingerprint,
                                &hlen);
    if (ret == -1) {
        error = NDEV_STATUS::GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not get server ssh public key hash, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }

    // TODO: We really should be first checking this is a known server to stop MITM attacks etc. before continuing
    // Minimally we could check the fingerprint is in a known list, as we don't really have known_hosts file.
    ssh_key_free(srv_pubkey);

    Debug_printf("SSH Host Key Fingerprint with length %d is: ", hlen);
    for (int i = 0; i < (int)hlen; i++)
    {
        Debug_printf("%02X", fingerprint[i]);
        if (i < (int)(hlen - 1))
            Debug_printf(":");
    }
    Debug_printf("\r\n");
    ssh_clean_pubkey_hash(&fingerprint);

    /* ---- Probe server auth methods ---- */
    ret = ssh_userauth_none(session, NULL);
    if (ret == SSH_AUTH_ERROR) {
        error = NDEV_STATUS::GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not issue 'none' userauth method to server, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* Log available methods */
    {
        int methods = ssh_userauth_list(session, NULL);
        Debug_printf("Authentication methods:\r\n"
                     "Password:    %s\r\n"
                     "Public Key:  %s\r\n"
                     "Host Based:  %s\r\n"
                     "Interactive: %s\r\n",
            (methods & SSH_AUTH_METHOD_PASSWORD) ? "true":"false",
            (methods & SSH_AUTH_METHOD_PUBLICKEY) ? "true":"false",
            (methods & SSH_AUTH_METHOD_HOSTBASED) ? "true":"false",
            (methods & SSH_AUTH_METHOD_INTERACTIVE) ? "true":"false"
        );
    }

    /* ---- Authenticate ---- */
    bool authOk;
    if (usePasswordAuth) {
        authOk = authenticateWithPassword();
    } else {
        authOk = authenticateWithDefaultKey();
    }

    if (!authOk) {
        ssh_disconnect(session);
        ssh_free(session);
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Open channel, PTY, shell ---- */
    channel = ssh_channel_new(session);
    if (channel == NULL) {
        error = NDEV_STATUS::GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not open new channel, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }
    ret = ssh_channel_open_session(channel);
    if (ret != SSH_OK) {
        error = NDEV_STATUS::GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not open session, error: %s.\r\n", message);
        return FUJI_ERROR::UNSPECIFIED;
    }

    ret = ssh_channel_request_pty_size(channel, "vanilla", 80, 24);
    if (ret != SSH_OK)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("Could not request pty\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    ret = ssh_channel_request_shell(channel);
    if (ret != SSH_OK)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("Could not open shell on channel\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    ssh_channel_set_blocking(channel, 0);

    // At this point, we should be able to talk to the shell.
    Debug_printf("Shell opened.\r\n");

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSSH::close()
{
    /*
     * Fix (2026-05-19): the previous implementation unconditionally
     * called ssh_disconnect()/ssh_free() on `session` without
     *   - tearing down `channel` first (ssh_channel_send_eof /
     *     ssh_channel_close / ssh_channel_free),
     *   - guarding against a NULL or already-freed `session`, and
     *   - chaining to NetworkProtocol::close() (which clears and
     *     shrinks the std::string buffers, like TCP::close() does).
     * That left libssh internal channel state half-freed when the
     * remote host had already closed the shell (after "exit\n"),
     * and a second CLOSE from the host would double-free the session.
     * Both cases corrupted the heap and crashed the next free().
     */
    if (channel != nullptr)
    {
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = nullptr;
    }

    if (session != nullptr)
    {
        ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }

    return NetworkProtocol::close();
}

fujiError_t NetworkProtocolSSH::read(unsigned short len)
{
    // Ironically, All of the read is handled in available().
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSSH::write(unsigned short len)
{
    fujiError_t err = FUJI_ERROR::NONE;

    len = translate_transmit_buffer();
    ssh_channel_write(channel, transmitBuffer->data(), len);

    // Return success - WTF?
    error = NDEV_STATUS::SUCCESS;
    transmitBuffer->erase(0, len);

    return err;
}

fujiError_t NetworkProtocolSSH::status(NetworkStatus *status)
{
    bool isEOF = ssh_channel_is_eof(channel) == 0;
    status->connected = isEOF ? 1 : 0;
    status->error = isEOF ? NDEV_STATUS::SUCCESS : NDEV_STATUS::END_OF_FILE;
    NetworkProtocol::status(status);
    return FUJI_ERROR::NONE;
}

size_t NetworkProtocolSSH::available()
{
    if (receiveBuffer->length() == 0)
    {
        if (channel != nullptr && ssh_channel_is_eof(channel) == 0)
        {
            int len = ssh_channel_read(channel, rxbuf, RXBUF_SIZE, 0);
            /*
             * Fix (2026-05-19): ssh_channel_read() returns a signed int
             * that can be SSH_AGAIN, SSH_EOF (0) or SSH_ERROR (-1).
             * The previous code only filtered SSH_AGAIN, so SSH_ERROR
             * (-1) was implicitly cast to size_t (~4 GB) inside
             * std::string::append(), trashing the PSRAM heap.  The
             * corruption only surfaced later as
             * "heap_caps_free target pointer is outside heap areas"
             * during ~NetworkProtocolSSH() in sio_close().
             * Accept only strictly positive byte counts.
             */
            if (len > 0)
            {
                receiveBuffer->append(rxbuf, (size_t)len);
                translate_receive_buffer();
            }
        }
    }

    return receiveBuffer->length();
}
