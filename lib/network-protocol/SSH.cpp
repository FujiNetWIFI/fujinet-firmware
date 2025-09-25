/**
 * SSH protocol implementation
 */

#include "SSH.h"

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <vector>

#define RXBUF_SIZE 65535

NetworkProtocolSSH::NetworkProtocolSSH(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolSSH::NetworkProtocolSSH(%p,%p,%p)\r\n", rx_buf, tx_buf, sp_buf);
#ifdef ESP_PLATFORM
    rxbuf = (char *)heap_caps_malloc(RXBUF_SIZE, MALLOC_CAP_SPIRAM);
#else
    rxbuf = (char *)malloc(RXBUF_SIZE);
#endif
}

NetworkProtocolSSH::~NetworkProtocolSSH()
{
    Debug_printf("NetworkProtocolSSH::~NetworkProtocolSSH()\r\n");
#ifdef ESP_PLATFORM
    heap_caps_free(rxbuf);
#else
    free(rxbuf);
#endif
}

bool NetworkProtocolSSH::open(PeoplesUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    NetworkProtocol::open(urlParser, cmdFrame);
    int ret;

    if (!urlParser->user.empty()) {
        login = &urlParser->user;
    }

    if (!urlParser->password.empty()) {
        password = &urlParser->password;
    }

    if (!login || !password || (login->empty() && password->empty()))
    {
        error = NETWORK_ERROR_INVALID_USERNAME_OR_PASSWORD;
        return true;
    }

    // Port 22 by default.
    if (urlParser->port.empty())
    {
        urlParser->port = "22";
    }

    if ((ret = ssh_init()) != 0)
    {
        Debug_printf("NetworkProtocolSSH::open() - ssh_init not successful. Value returned: %d\r\n", ret);
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    Debug_printf("NetworkProtocolSSH::open() - Opening session.\r\n");
    session = ssh_new();
    if (session == NULL)
    {
        Debug_printf("Could not create session. aborting.\r\n");
        error = NETWORK_ERROR_NOT_CONNECTED;
        return true;
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
        error = NETWORK_ERROR_NOT_CONNECTED;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not connect, error: %s.\r\n", message);
        return true;
    }

    ssh_key srv_pubkey = NULL;
    ret = ssh_get_server_publickey(session, &srv_pubkey);
    if (ret < 0) {
        error = NETWORK_ERROR_GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not get server ssh public key, error: %s.\r\n", message);
        return true;
    }

    size_t hlen;
    ret = ssh_get_publickey_hash(srv_pubkey,
                                SSH_PUBLICKEY_HASH_SHA1,
                                &fingerprint,
                                &hlen);
    if (ret == -1) {
        error = NETWORK_ERROR_GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not get server ssh public key hash, error: %s.\r\n", message);
        return true;
    }
    
    // TODO: We really should be first checking this is a known server to stop MITM attacks etc. before continuing
    // Minimally we could check the fingerprint is in a known list, as we don't really have known_hosts file.
    ssh_key_free(srv_pubkey);

    Debug_printf("SSH Host Key Fingerprint with length %d is: ", hlen);
    // ODE FOR string.join();
    for (int i = 0; i < hlen; i++)
    {
        Debug_printf("%02X", fingerprint[i]);
        if (i < (hlen - 1))
            Debug_printf(":");
    }
    Debug_printf("\r\n");
    ssh_clean_pubkey_hash(&fingerprint);


    ret = ssh_userauth_none(session, NULL);
    // TODO: Are we in blocking mode? If we are not, then we will have to deal with SSH_AUTH_AGAIN
    if (ret == SSH_AUTH_ERROR) {
        error = NETWORK_ERROR_GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not issue 'none' userauth method to server, error: %s.\r\n", message);
        return true;
    }

    ret = ssh_userauth_list(session, NULL);
    bool allowsPassword = ret & SSH_AUTH_METHOD_PASSWORD;
    bool allowsPublicKey = ret & SSH_AUTH_METHOD_PUBLICKEY;
    bool allowsHostBased = ret & SSH_AUTH_METHOD_HOSTBASED;
    bool allowsInteractive = ret & SSH_AUTH_METHOD_INTERACTIVE;
    Debug_printf("Authentication methods:\r\n"
                 "Password:    %s\r\n"
                 "Public Key:  %s\r\n"
                 "Host Based:  %s\r\n"
                 "Interactive: %s\r\n", 
        allowsPassword ? "true":"false",
        allowsPublicKey ? "true":"false",
        allowsHostBased ? "true":"false",
        allowsInteractive ? "true":"false"
    );

    if (!allowsPassword) {
        // May as well stop here, as our only ability (password) isn't allowed
        error = NETWORK_ERROR_GENERAL;
        Debug_printf("NetworkProtocolSSH::open() - Could not login to server as it does not allow password auth.\r\n");
        return true;
    }

    ret = ssh_userauth_password(session, NULL, password->c_str());
    // SSH_AUTH_AGAIN may need to be handled here too, if we're in non-blocking mode.

    if (ret != SSH_AUTH_SUCCESS) {
        error = NETWORK_ERROR_ACCESS_DENIED;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Unable to authorise with given password, error: %s.\r\n", message);
        ssh_disconnect(session);
        ssh_free(session);
    }

    channel = ssh_channel_new(session);
    if (channel == NULL) {
        error = NETWORK_ERROR_GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not open new channel, error: %s.\r\n", message);
        return true;
    }
    ret = ssh_channel_open_session(channel);
    if (ret != SSH_OK) {
        error = NETWORK_ERROR_GENERAL;
        const char *message = ssh_get_error(session);
        Debug_printf("NetworkProtocolSSH::open() - Could not open session, error: %s.\r\n", message);
        return true;
    }


    ret = ssh_channel_request_pty_size(channel, "vanilla", 80, 24);
    if (ret != SSH_OK)
    {
        error = NETWORK_ERROR_GENERAL;
        Debug_printf("Could not request pty\r\n");
        return true;
    }

    ret = ssh_channel_request_shell(channel);
    if (ret != SSH_OK)
    {
        error = NETWORK_ERROR_GENERAL;
        Debug_printf("Could not open shell on channel\r\n");
        return true;
    }

    ssh_channel_set_blocking(channel, 0);

    // At this point, we should be able to talk to the shell.
    Debug_printf("Shell opened.\r\n");

    return false;
}

bool NetworkProtocolSSH::close()
{
    ssh_disconnect(session);
    ssh_free(session);
    return false;
}

bool NetworkProtocolSSH::read(unsigned short len)
{
    // Ironically, All of the read is handled in available().
    return false;
}

bool NetworkProtocolSSH::write(unsigned short len)
{
    bool err = false;

    len = translate_transmit_buffer();
    ssh_channel_write(channel, transmitBuffer->data(), len);

    // Return success - WTF?
    error = 1;
    transmitBuffer->erase(0, len);

    return err;
}

bool NetworkProtocolSSH::status(NetworkStatus *status)
{
    bool isEOF = ssh_channel_is_eof(channel) == 0;
    status->connected = isEOF ? 1 : 0;
    status->error = isEOF ? 1 : NETWORK_ERROR_END_OF_FILE;
    NetworkProtocol::status(status);
    return false;
}

uint8_t NetworkProtocolSSH::special_inquiry(uint8_t cmd)
{
    return 0xFF; // selected command not implemented.
}

bool NetworkProtocolSSH::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSSH::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSSH::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

size_t NetworkProtocolSSH::available()
{
    if (receiveBuffer->length() == 0)
    {
        if (ssh_channel_is_eof(channel) == 0)
        {
            int len = ssh_channel_read(channel, rxbuf, RXBUF_SIZE, 0);
            if (len != SSH_AGAIN)
            {
                receiveBuffer->append(rxbuf, len);
                translate_receive_buffer();
            }
        }
    }

    return receiveBuffer->length();
}
