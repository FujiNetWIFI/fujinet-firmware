/**
 * SSH.KEYGEN protocol implementation
 *
 * Generates an Ed25519 SSH key pair on the FujiNet SD card.
 *
 * URL:   N:SSH.KEYGEN://ed25519/             (fail if key exists)
 *        N:SSH.KEYGEN://ed25519/?overwrite=1 (replace existing key)
 *
 * Keys:  /.ssh/id_ed25519      (private, OpenSSH format)
 *        /.ssh/id_ed25519.pub  (public, authorized_keys format)
 */

#include "SSHKeygen.h"

#include "../../include/debug.h"
#include "status_error_codes.h"

#include <libssh/libssh.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <algorithm>

/* ------------------------------------------------------------------ */
/* SD card base path — same convention as SSH.cpp                     */
/* ------------------------------------------------------------------ */
#ifdef ESP_PLATFORM
#define SD_BASE_PATH "/sd"
#else
#define SD_BASE_PATH "SD"
#endif

#define SSH_DIR_REL      "/.ssh"
#define PRIVKEY_FILENAME "/id_ed25519"
#define PUBKEY_FILENAME  "/id_ed25519.pub"
#define PUBKEY_COMMENT   "FujiNet"

/* ------------------------------------------------------------------ */
/* ctor / dtor                                                        */
/* ------------------------------------------------------------------ */
NetworkProtocolSSHKeygen::NetworkProtocolSSHKeygen(std::string *rx_buf,
                                                   std::string *tx_buf,
                                                   std::string *sp_buf)
    : NetworkProtocol(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolSSHKeygen::ctor(%p,%p,%p)\r\n",
                 rx_buf, tx_buf, sp_buf);
}

NetworkProtocolSSHKeygen::~NetworkProtocolSSHKeygen()
{
    Debug_printf("NetworkProtocolSSHKeygen::~dtor()\r\n");
    statusResponse.clear();
}

/* ------------------------------------------------------------------ */
/* Path helpers                                                       */
/* ------------------------------------------------------------------ */
std::string NetworkProtocolSSHKeygen::getSshDirPath()
{
    return std::string(SD_BASE_PATH) + SSH_DIR_REL;
}

std::string NetworkProtocolSSHKeygen::getPrivateKeyPath()
{
    return getSshDirPath() + PRIVKEY_FILENAME;
}

std::string NetworkProtocolSSHKeygen::getPublicKeyPath()
{
    return getSshDirPath() + PUBKEY_FILENAME;
}

/* ------------------------------------------------------------------ */
/* Ensure /.ssh directory exists                                      */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHKeygen::ensureSshDirectoryExists()
{
    std::string dirPath = getSshDirPath();
    struct stat st;

    if (stat(dirPath.c_str(), &st) == 0) {
        /* Path exists — check it's a directory */
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        Debug_printf("SSH.KEYGEN: %s exists but is not a directory.\r\n",
                     dirPath.c_str());
        return false;
    }

    /* Directory does not exist — create it */
    Debug_printf("SSH.KEYGEN: creating directory %s\r\n", dirPath.c_str());
#ifdef _WIN32
    if (mkdir(dirPath.c_str()) != 0) {
#else
    if (mkdir(dirPath.c_str(), 0755) != 0) {
#endif
        Debug_printf("SSH.KEYGEN: mkdir(%s) failed.\r\n", dirPath.c_str());
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ */
/* Check if key files already exist                                   */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHKeygen::keyFilesAlreadyExist()
{
    struct stat st;
    std::string privPath = getPrivateKeyPath();
    std::string pubPath  = getPublicKeyPath();

    if (stat(privPath.c_str(), &st) == 0) {
        Debug_printf("SSH.KEYGEN: private key already exists: %s\r\n",
                     privPath.c_str());
        return true;
    }
    if (stat(pubPath.c_str(), &st) == 0) {
        Debug_printf("SSH.KEYGEN: public key already exists: %s\r\n",
                     pubPath.c_str());
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Write public key in authorized_keys one-line format                */
/*   ssh-ed25519 AAAA...base64... FujiNet\n                           */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHKeygen::writePublicKey(void *key,
                                              const std::string &path)
{
    ssh_key sshKey = (ssh_key)key;
    char *b64_key = NULL;

    int rc = ssh_pki_export_pubkey_base64(sshKey, &b64_key);
    if (rc != SSH_OK || b64_key == NULL) {
        Debug_printf("SSH.KEYGEN: ssh_pki_export_pubkey_base64 failed.\r\n");
        return false;
    }

    const char *type_str = ssh_key_type_to_char(ssh_key_type(sshKey));
    if (type_str == NULL) {
        Debug_printf("SSH.KEYGEN: ssh_key_type_to_char failed.\r\n");
        free(b64_key);
        return false;
    }

    FILE *fp = fopen(path.c_str(), "w");
    if (fp == NULL) {
        Debug_printf("SSH.KEYGEN: cannot open %s for writing.\r\n",
                     path.c_str());
        free(b64_key);
        return false;
    }

    fprintf(fp, "%s %s %s\n", type_str, b64_key, PUBKEY_COMMENT);
    fclose(fp);
    free(b64_key);

    Debug_printf("SSH.KEYGEN: public key written to %s\r\n", path.c_str());
    return true;
}

/* ------------------------------------------------------------------ */
/* Remove a file, ignoring errors if it doesn't exist.                */
/* ------------------------------------------------------------------ */
void NetworkProtocolSSHKeygen::removeFile(const std::string &path)
{
    remove(path.c_str());
}

/* ------------------------------------------------------------------ */
/* Parse query string for overwrite=1.                                */
/* Only the exact value "1" enables overwrite.                        */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHKeygen::parseOverwrite(const std::string &query)
{
    /* Look for "overwrite=1" in the query string.
       Query may contain multiple params separated by '&'. */
    std::string::size_type pos = 0;
    while (pos < query.length()) {
        std::string::size_type amp = query.find('&', pos);
        std::string param = (amp == std::string::npos)
            ? query.substr(pos)
            : query.substr(pos, amp - pos);

        if (param == "overwrite=1")
            return true;

        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Generate Ed25519 key pair and save to SD card.                     */
/*                                                                    */
/* When useTempFiles is true (overwrite mode), writes to .tmp files   */
/* first, then renames both into place.  This avoids leaving          */
/* mismatched private/public key files if a write fails partway.      */
/*                                                                    */
/* Note: rename() on FAT/POSIX is not atomic but is the safest       */
/* available sequence — both files are only replaced after both       */
/* temp writes succeed.                                               */
/* ------------------------------------------------------------------ */
bool NetworkProtocolSSHKeygen::generateEd25519KeyPair(bool useTempFiles)
{
    ssh_key key = NULL;
    int rc;

    Debug_printf("SSH.KEYGEN: generating Ed25519 key pair...\r\n");

    /* Generate key — parameter 0 for Ed25519 (no variable size) */
    rc = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key);
    if (rc != SSH_OK || key == NULL) {
        Debug_printf("SSH.KEYGEN: ssh_pki_generate(ED25519) failed (rc=%d).\r\n", rc);
        return false;
    }

    std::string privPath = getPrivateKeyPath();
    std::string pubPath  = getPublicKeyPath();

    /* Paths to write to — either final or temporary */
    std::string privWrite = useTempFiles ? (privPath + ".tmp") : privPath;
    std::string pubWrite  = useTempFiles ? (pubPath  + ".tmp") : pubPath;

    /* Write private key (OpenSSH format, no passphrase) */
    Debug_printf("SSH.KEYGEN: writing private key to %s\r\n", privWrite.c_str());
    rc = ssh_pki_export_privkey_file(key, NULL, NULL, NULL, privWrite.c_str());
    if (rc != SSH_OK) {
        Debug_printf("SSH.KEYGEN: ssh_pki_export_privkey_file failed (rc=%d).\r\n", rc);
        if (useTempFiles) removeFile(privWrite);
        ssh_key_free(key);
        return false;
    }

    /* Write public key (authorized_keys format with comment) */
    if (!writePublicKey(key, pubWrite)) {
        if (useTempFiles) removeFile(privWrite);
        ssh_key_free(key);
        return false;
    }

    ssh_key_free(key);

    /* If using temp files, rename both into final position.
       Remove old files first (rename over existing may fail on some
       filesystems like FAT). */
    if (useTempFiles) {
        removeFile(privPath);
        removeFile(pubPath);

        if (rename(privWrite.c_str(), privPath.c_str()) != 0) {
            Debug_printf("SSH.KEYGEN: rename %s -> %s failed.\r\n",
                         privWrite.c_str(), privPath.c_str());
            removeFile(privWrite);
            removeFile(pubWrite);
            return false;
        }
        if (rename(pubWrite.c_str(), pubPath.c_str()) != 0) {
            Debug_printf("SSH.KEYGEN: rename %s -> %s failed.\r\n",
                         pubWrite.c_str(), pubPath.c_str());
            /* Private key already renamed — remove it to avoid mismatch */
            removeFile(privPath);
            removeFile(pubWrite);
            return false;
        }
    }

    Debug_printf("SSH.KEYGEN: key pair generated successfully.\r\n");
    return true;
}

/* ------------------------------------------------------------------ */
/* open() — parse URL, validate, generate keys                        */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHKeygen::open(PeoplesUrlParser *urlParser,
                                           fileAccessMode_t access,
                                           netProtoTranslation_t translate)
{
    NetworkProtocol::open(urlParser, access, translate);

    /* The algorithm is in the host field of the URL:
       N:SSH.KEYGEN://ed25519/  =>  host = "ed25519" */
    std::string algo = urlParser->host;
    std::transform(algo.begin(), algo.end(), algo.begin(), ::tolower);

    /* Parse overwrite flag from query string:
       N:SSH.KEYGEN://ed25519/?overwrite=1 */
    bool overwrite = parseOverwrite(urlParser->query);

    Debug_printf("SSH.KEYGEN algorithm: %s\r\n", algo.c_str());
    Debug_printf("SSH.KEYGEN overwrite: %s\r\n", overwrite ? "yes" : "no");

    /* Only Ed25519 is supported */
    if (algo != "ed25519") {
        statusResponse = "SSH.KEYGEN error: unsupported algorithm: " + algo + "\r\n";
        error = NDEV_STATUS::GENERAL;
        Debug_printf("SSH.KEYGEN: unsupported algorithm '%s'.\r\n", algo.c_str());
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* Ensure /.ssh directory exists */
    if (!ensureSshDirectoryExists()) {
        statusResponse = "SSH.KEYGEN error: cannot create .ssh directory\r\n";
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* Check for existing keys */
    bool keysExist = keyFilesAlreadyExist();
    if (keysExist && !overwrite) {
        statusResponse = "SSH.KEYGEN error: key already exists\r\n";
        error = NDEV_STATUS::GENERAL;
        Debug_printf("SSH.KEYGEN: refusing to overwrite existing keys.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* Generate the key pair.
       When overwriting, use temp files to avoid leaving mismatched
       private/public keys if a write fails partway. */
    if (!generateEd25519KeyPair(keysExist && overwrite)) {
        statusResponse = "SSH.KEYGEN error: key generation failed\r\n";
        error = NDEV_STATUS::GENERAL;
        return FUJI_ERROR::UNSPECIFIED;
    }

    /* Build success status response */
    std::string privRel = std::string(SSH_DIR_REL) + PRIVKEY_FILENAME;
    std::string pubRel  = std::string(SSH_DIR_REL) + PUBKEY_FILENAME;

    statusResponse  = "OK SSH.KEYGEN\r\n";
    statusResponse += "TYPE ed25519\r\n";
    statusResponse += std::string("OVERWRITE ") + (overwrite ? "1" : "0") + "\r\n";
    statusResponse += "PRIVATE " + privRel + "\r\n";
    statusResponse += "PUBLIC "  + pubRel  + "\r\n";

    Debug_printf("SSH.KEYGEN: success.\r\n");
    Debug_printf("SSH.KEYGEN private key path: %s\r\n", getPrivateKeyPath().c_str());
    Debug_printf("SSH.KEYGEN public key path: %s\r\n",  getPublicKeyPath().c_str());

    return FUJI_ERROR::NONE;
}

/* ------------------------------------------------------------------ */
/* read() — return status response                                    */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHKeygen::read(unsigned short len)
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
fujiError_t NetworkProtocolSSHKeygen::write(unsigned short len)
{
    error = NDEV_STATUS::GENERAL;
    return FUJI_ERROR::UNSPECIFIED;
}

/* ------------------------------------------------------------------ */
/* status()                                                           */
/* ------------------------------------------------------------------ */
fujiError_t NetworkProtocolSSHKeygen::status(NetworkStatus *status)
{
    status->connected = 1;
    status->error = error;
    NetworkProtocol::status(status);
    return FUJI_ERROR::NONE;
}
