/**
 * SSH.COPYID protocol implementation
 *
 * Installs the FujiNet default SSH public key on a remote server.
 *
 * URL:   N:SSH.COPYID://user:pass@host:port/
 *
 * Reads /.ssh/id_ed25519.pub from SD card, connects to the remote
 * host using password authentication, and appends the public key
 * to ~/.ssh/authorized_keys (skipping if already present).
 */

#include "SSHCopyId.h"

#include "../../include/debug.h"
#include "status_error_codes.h"

#include <libssh/libssh.h>

#include <cstdio>
#include <cstring>
#include <algorithm>

/* ------------------------------------------------------------------ */
/* SD card base path — same convention as SSH.cpp / SSHKeygen.cpp     */
/* ------------------------------------------------------------------ */
#ifdef ESP_PLATFORM
#define SD_BASE_PATH "/sd"
#else
#define SD_BASE_PATH "SD"
#endif

#define PUBKEY_REL_PATH "/.ssh/id_ed25519.pub"

/* ------------------------------------------------------------------ */
/* ctor / dtor                                                        */
/* ------------------------------------------------------------------ */
NetworkProtocolSSHCopyId::NetworkProtocolSSHCopyId(std::string *rx_buf,
                                                   std::string *tx_buf,
                                                   std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolSSHCopyId::ctor(%p,%p,%p)\r\n",
                 rx_buf, tx_buf, sp_buf);
}

NetworkProtocolSSHCopyId::~NetworkProtocolSSHCopyId()
{
    Debug_printf("NetworkProtocolSSHCopyId::~dtor()\r\n");
    statusResponse.clear();
}

/* ------------------------------------------------------------------ */
/* Path helper                                                        */
/* ------------------------------------------------------------------ */
std::string NetworkProtocolSSHCopyId::getPublicKeyPath()
{
    return std::string(SD_BASE_PATH) + PUBKEY_REL_PATH;
}

/* ------------------------------------------------------------------ */
/* Read public key from SD card                                       */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHCopyId::readDefaultPublicKey(std::string &pubkeyLine)
{
    std::string path = getPublicKeyPath();
    Debug_printf("SSH.COPYID: reading public key from %s\r\n", path.c_str());

    FILE *fp = fopen(path.c_str(), "r");
    if (fp == NULL) {
        Debug_printf("SSH.COPYID: cannot open %s\r\n", path.c_str());
        return false;
    }

    char buf[1024];
    pubkeyLine.clear();

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        pubkeyLine += buf;
    }
    fclose(fp);

    /* Trim trailing whitespace / newlines */
    while (!pubkeyLine.empty() &&
           (pubkeyLine.back() == '\n' || pubkeyLine.back() == '\r' ||
            pubkeyLine.back() == ' '  || pubkeyLine.back() == '\t')) {
        pubkeyLine.pop_back();
    }

    if (pubkeyLine.empty()) {
        Debug_printf("SSH.COPYID: public key file is empty.\r\n");
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Validate Ed25519 public key format                                 */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHCopyId::validateEd25519PublicKey(const std::string &line)
{
    const char *prefix = "ssh-ed25519 ";
    size_t prefixLen = strlen(prefix);

    if (line.length() < prefixLen + 10) {
        return false;
    }

    if (line.compare(0, prefixLen, prefix) != 0) {
        return false;
    }

    /* Check that there is base64 content after the prefix */
    size_t spacePos = line.find(' ', prefixLen);
    std::string b64;
    if (spacePos == std::string::npos) {
        b64 = line.substr(prefixLen);
    } else {
        b64 = line.substr(prefixLen, spacePos - prefixLen);
    }

    if (b64.length() < 10) {
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Shell-quote a string using single quotes                           */
/*                                                                    */
/* 'hello'        => 'hello'                                          */
/* it's           => 'it'"'"'s'                                       */
/* ------------------------------------------------------------------ */
std::string NetworkProtocolSSHCopyId::shellQuoteSingle(const std::string &s)
{
    std::string result = "'";
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '\'') {
            /* End current single-quoted segment, add escaped quote,
               start new single-quoted segment */
            result += "'\"'\"'";
        } else {
            result += s[i];
        }
    }
    result += "'";
    return result;
}

/* ------------------------------------------------------------------ */
/* Build remote install command                                       */
/* ------------------------------------------------------------------ */
std::string NetworkProtocolSSHCopyId::buildRemoteInstallCommand(
    const std::string &pubkeyLine)
{
    std::string quoted = shellQuoteSingle(pubkeyLine);

    std::string cmd;
    cmd  = "umask 077; ";
    cmd += "mkdir -p \"$HOME/.ssh\"; ";
    cmd += "touch \"$HOME/.ssh/authorized_keys\"; ";
    cmd += "chmod 700 \"$HOME/.ssh\"; ";
    cmd += "chmod 600 \"$HOME/.ssh/authorized_keys\"; ";
    cmd += "if grep -qxF " + quoted + " \"$HOME/.ssh/authorized_keys\"; then ";
    cmd += "echo ALREADY_PRESENT; ";
    cmd += "else ";
    cmd += "printf '%s\\n' " + quoted + " >> \"$HOME/.ssh/authorized_keys\"; ";
    cmd += "chmod 600 \"$HOME/.ssh/authorized_keys\"; ";
    cmd += "echo INSTALLED; ";
    cmd += "fi";

    return cmd;
}

/* ------------------------------------------------------------------ */
/* Connect to remote host with password authentication                */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHCopyId::connectWithPassword(
    const std::string &host, int port,
    const std::string &user, const std::string &pass,
    ssh_session &sess)
{
    int ret;

    ret = ssh_init();
    if (ret != 0) {
        Debug_printf("SSH.COPYID: ssh_init failed (rc=%d).\r\n", ret);
        setErrorResponse("SSH.COPYID error: SSH library init failed");
        return false;
    }

    sess = ssh_new();
    if (sess == NULL) {
        Debug_printf("SSH.COPYID: ssh_new failed.\r\n");
        setErrorResponse("SSH.COPYID error: cannot create SSH session");
        return false;
    }

    int verbosity = SSH_LOG_PROTOCOL;
    ssh_options_set(sess, SSH_OPTIONS_USER, user.c_str());
    ssh_options_set(sess, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(sess, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(sess, SSH_OPTIONS_PORT, &port);
#ifdef ESP_PLATFORM
    sess->opts.config_processed = true;
#endif

    Debug_printf("SSH.COPYID: connecting to %s:%d as %s\r\n",
                 host.c_str(), port, user.c_str());

    ret = ssh_connect(sess);
    if (ret != SSH_OK) {
        const char *msg = ssh_get_error(sess);
        Debug_printf("SSH.COPYID: ssh_connect failed: %s\r\n", msg);
        setErrorResponse("SSH.COPYID error: cannot connect to host");
        ssh_free(sess);
        sess = NULL;
        return false;
    }

    /* Host key verification — get and log fingerprint */
    ssh_key srv_pubkey = NULL;
    ret = ssh_get_server_publickey(sess, &srv_pubkey);
    if (ret < 0) {
        Debug_printf("SSH.COPYID: cannot get server public key.\r\n");
        setErrorResponse("SSH.COPYID error: cannot verify host key");
        ssh_disconnect(sess);
        ssh_free(sess);
        sess = NULL;
        return false;
    }

    unsigned char *hash = NULL;
    size_t hlen;
    ret = ssh_get_publickey_hash(srv_pubkey, SSH_PUBLICKEY_HASH_SHA1,
                                 &hash, &hlen);
    ssh_key_free(srv_pubkey);

    if (ret == 0 && hash != NULL) {
        Debug_printf("SSH.COPYID host key fingerprint: ");
        for (size_t i = 0; i < hlen; i++) {
            Debug_printf("%02X%s", hash[i], (i < hlen - 1) ? ":" : "");
        }
        Debug_printf("\r\n");
        ssh_clean_pubkey_hash(&hash);
    }

    /* Probe server auth methods */
    ret = ssh_userauth_none(sess, NULL);
    if (ret == SSH_AUTH_ERROR) {
        Debug_printf("SSH.COPYID: userauth_none failed.\r\n");
        setErrorResponse("SSH.COPYID error: authentication handshake failed");
        ssh_disconnect(sess);
        ssh_free(sess);
        sess = NULL;
        return false;
    }

    int methods = ssh_userauth_list(sess, NULL);
    if (!(methods & SSH_AUTH_METHOD_PASSWORD)) {
        Debug_printf("SSH.COPYID: server does not allow password auth.\r\n");
        setErrorResponse("SSH.COPYID error: server does not allow password authentication");
        ssh_disconnect(sess);
        ssh_free(sess);
        sess = NULL;
        return false;
    }

    /* Authenticate with password */
    ret = ssh_userauth_password(sess, NULL, pass.c_str());
    if (ret != SSH_AUTH_SUCCESS) {
        Debug_printf("SSH.COPYID: password auth failed.\r\n");
        setErrorResponse("SSH.COPYID error: authentication failed");
        ssh_disconnect(sess);
        ssh_free(sess);
        sess = NULL;
        return false;
    }

    Debug_printf("SSH.COPYID: authenticated successfully.\r\n");
    return true;
}

/* ------------------------------------------------------------------ */
/* Execute remote install command                                     */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHCopyId::installPublicKey(
    ssh_session sess, const std::string &pubkeyLine, bool &installed)
{
    int ret;

    ssh_channel chan = ssh_channel_new(sess);
    if (chan == NULL) {
        Debug_printf("SSH.COPYID: cannot create channel.\r\n");
        setErrorResponse("SSH.COPYID error: cannot open SSH channel");
        return false;
    }

    ret = ssh_channel_open_session(chan);
    if (ret != SSH_OK) {
        Debug_printf("SSH.COPYID: cannot open channel session.\r\n");
        setErrorResponse("SSH.COPYID error: cannot open SSH channel");
        ssh_channel_free(chan);
        return false;
    }

    std::string cmd = buildRemoteInstallCommand(pubkeyLine);
    Debug_printf("SSH.COPYID: executing remote command (%u bytes).\r\n",
                 (unsigned)cmd.length());

    ret = ssh_channel_request_exec(chan, cmd.c_str());
    if (ret != SSH_OK) {
        Debug_printf("SSH.COPYID: channel_request_exec failed.\r\n");
        setErrorResponse("SSH.COPYID error: cannot execute remote command");
        ssh_channel_close(chan);
        ssh_channel_free(chan);
        return false;
    }

    /* Read stdout */
    char buf[256];
    std::string output;
    int nbytes;

    do {
        nbytes = ssh_channel_read(chan, buf, sizeof(buf) - 1, 0);
        if (nbytes > 0) {
            buf[nbytes] = '\0';
            output += buf;
        }
    } while (nbytes > 0);

    /* Read stderr for diagnostics */
    std::string stderr_output;
    do {
        nbytes = ssh_channel_read(chan, buf, sizeof(buf) - 1, 1);
        if (nbytes > 0) {
            buf[nbytes] = '\0';
            stderr_output += buf;
        }
    } while (nbytes > 0);

    if (!stderr_output.empty()) {
        Debug_printf("SSH.COPYID: remote stderr: %s\r\n", stderr_output.c_str());
    }

    /* Wait for command to finish and get exit status */
    ssh_channel_send_eof(chan);
    ssh_channel_close(chan);

    int exit_status = ssh_channel_get_exit_status(chan);
    Debug_printf("SSH.COPYID: remote exit status: %d\r\n", exit_status);
    Debug_printf("SSH.COPYID: remote stdout: %s\r\n", output.c_str());

    ssh_channel_free(chan);

    if (exit_status != 0) {
        Debug_printf("SSH.COPYID: remote command failed (exit=%d).\r\n",
                     exit_status);
        setErrorResponse("SSH.COPYID error: remote install failed");
        return false;
    }

    /* Parse output to determine if key was installed or already present */
    if (output.find("INSTALLED") != std::string::npos) {
        installed = true;
    } else if (output.find("ALREADY_PRESENT") != std::string::npos) {
        installed = false;
    } else {
        /* Unexpected output — treat as success but log */
        Debug_printf("SSH.COPYID: unexpected remote output, assuming installed.\r\n");
        installed = true;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Status response helpers                                            */
/* ------------------------------------------------------------------ */
void NetworkProtocolSSHCopyId::setStatusResponse(
    const std::string &host, const std::string &user, bool installed)
{
    statusResponse  = "OK SSH.COPYID\r\n";
    statusResponse += "HOST " + host + "\r\n";
    statusResponse += "USER " + user + "\r\n";
    statusResponse += "KEY " + std::string(PUBKEY_REL_PATH) + "\r\n";
    statusResponse += std::string("STATUS ") +
                      (installed ? "installed" : "already-present") + "\r\n";
}

void NetworkProtocolSSHCopyId::setErrorResponse(const std::string &msg)
{
    statusResponse = msg + "\r\n";
}

/* ------------------------------------------------------------------ */
/* open() — main entry point                                          */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHCopyId::open(PeoplesUrlParser *urlParser,
                                           fileAccessMode_t access,
                                           netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);

    /* ---- Parse credentials from URL ---- */
    if (!urlParser->user.empty()) {
        login = &urlParser->user;
    }
    if (!urlParser->password.empty()) {
        password = &urlParser->password;
    }

    std::string host = urlParser->host;
    std::string user = login ? *login : "";
    std::string pass = password ? *password : "";

    /* Default port */
    if (urlParser->port.empty()) {
        urlParser->port = "22";
    }
    int port = urlParser->getPort();

    Debug_printf("SSH.COPYID host: %s\r\n", host.c_str());
    Debug_printf("SSH.COPYID user: %s\r\n", user.c_str());
    Debug_printf("SSH.COPYID port: %d\r\n", port);

    /* ---- Validate URL ---- */
    if (user.empty()) {
        setErrorResponse("SSH.COPYID error: missing SSH username");
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        Debug_printf("SSH.COPYID: missing username.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (pass.empty()) {
        setErrorResponse("SSH.COPYID error: password required");
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        Debug_printf("SSH.COPYID: password required.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (host.empty()) {
        setErrorResponse("SSH.COPYID error: missing hostname");
        error = NDEV_STATUS::GENERAL;
        Debug_printf("SSH.COPYID: missing hostname.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Read and validate local public key ---- */
    std::string pubkeyLine;
    if (!readDefaultPublicKey(pubkeyLine)) {
        setErrorResponse("SSH.COPYID error: public key not found");
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    Debug_printf("SSH.COPYID public key path: %s\r\n", getPublicKeyPath().c_str());

    if (!validateEd25519PublicKey(pubkeyLine)) {
        setErrorResponse("SSH.COPYID error: invalid public key");
        error = NDEV_STATUS::GENERAL;
        Debug_printf("SSH.COPYID: public key validation failed.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Connect and authenticate ---- */
    ssh_session sess = NULL;
    if (!connectWithPassword(host, port, user, pass, sess)) {
        /* Error response already set by connectWithPassword */
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Install public key on remote host ---- */
    bool installed = false;
    bool ok = installPublicKey(sess, pubkeyLine, installed);

    /* ---- Clean up SSH session ---- */
    ssh_disconnect(sess);
    ssh_free(sess);

    if (!ok) {
        /* Error response already set by installPublicKey */
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* ---- Build success response ---- */
    setStatusResponse(host, user, installed);

    Debug_printf("SSH.COPYID: success — %s.\r\n",
                 installed ? "key installed" : "key already present");

    return FUJI_ERROR::NONE;
}

/* ------------------------------------------------------------------ */
/* read() — return status response                                    */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHCopyId::read(unsigned short len)
{
    if (receiveBuffer->length() == 0 && !statusResponse.empty()) {
        *receiveBuffer += statusResponse.substr(0, len);
        if (len < statusResponse.length())
            statusResponse.erase(0, len);
        else
            statusResponse.clear();
    }

    error = NDEV_STATUS::SUCCESS;
    return NetworkProtocol::read(len);
}

/* ------------------------------------------------------------------ */
/* write() — not supported                                            */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHCopyId::write(unsigned short len)
{
    error = NDEV_STATUS::GENERAL;
    return FUJI_ERROR::UNSPECIFIED;
}

/* ------------------------------------------------------------------ */
/* status()                                                           */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHCopyId::status(NetworkStatus *status)
{
    status->connected = 1;
    status->error = error;
    NetworkProtocol::status(status);
    return FUJI_ERROR::NONE;
}
