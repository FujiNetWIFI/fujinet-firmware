/**
 * NetworkProtocolSFTP
 *
 * SFTP (SSH File Transfer Protocol) adapter, layered on libssh's SFTP
 * subsystem. Structurally identical to the other NetworkProtocolFS
 * adapters (TNFS/SMB/NFS); the SSH connect + authenticate flow mirrors
 * SSH.cpp.
 *
 * Auth mode is inferred from the URL, exactly like SSH:
 *
 *   N:SFTP://user:pass@host:port/path  => password authentication
 *   N:SFTP://user@host:port/path       => public-key authentication
 *                                         (/.ssh/id_ed25519 on SD card)
 */

#include "SFTP.h"

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <fcntl.h>

#ifdef ESP_PLATFORM
// apc: access to private member opts.config_processed (see SSH.cpp)
#include "libssh/session.h"
#endif

#include <cstring>

/**
 * Default SSH private key path relative to SD card root.
 */
#define SFTP_DEFAULT_KEY_REL "/.ssh/id_ed25519"

/**
 * SD card base path differs between ESP and PC builds.
 */
#ifdef ESP_PLATFORM
#define SD_BASE_PATH "/sd"
#else
#define SD_BASE_PATH "SD"
#endif

NetworkProtocolSFTP::NetworkProtocolSFTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSFTP::ctor\r\n");
}

NetworkProtocolSFTP::~NetworkProtocolSFTP()
{
    Debug_printf("NetworkProtocolSFTP::dtor\r\n");
}

std::string NetworkProtocolSFTP::getDefaultPrivateKeyPath()
{
    return std::string(SD_BASE_PATH) + SFTP_DEFAULT_KEY_REL;
}

bool NetworkProtocolSFTP::authenticateWithPassword()
{
    Debug_printf("SFTP auth mode: password\r\n");

    int methods = ssh_userauth_list(session, NULL);
    if (!(methods & SSH_AUTH_METHOD_PASSWORD))
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::authenticateWithPassword() - Server does not allow password auth.\r\n");
        return false;
    }

    if (ssh_userauth_password(session, NULL, password->c_str()) != SSH_AUTH_SUCCESS)
    {
        error = NDEV_STATUS::ACCESS_DENIED;
        Debug_printf("NetworkProtocolSFTP::authenticateWithPassword() - failed: %s\r\n", ssh_get_error(session));
        return false;
    }

    return true;
}

bool NetworkProtocolSFTP::authenticateWithDefaultKey()
{
    Debug_printf("SFTP auth mode: publickey\r\n");

    int methods = ssh_userauth_list(session, NULL);
    if (!(methods & SSH_AUTH_METHOD_PUBLICKEY))
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::authenticateWithDefaultKey() - Server does not allow public key auth.\r\n");
        return false;
    }

    std::string keyPath = getDefaultPrivateKeyPath();
    Debug_printf("SFTP private key: %s\r\n", keyPath.c_str());

    ssh_key privkey = NULL;
    if (ssh_pki_import_privkey_file(keyPath.c_str(), NULL, NULL, NULL, &privkey) != SSH_OK)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::authenticateWithDefaultKey() - could not load key from %s\r\n", keyPath.c_str());
        return false;
    }

    int ret = ssh_userauth_publickey(session, NULL, privkey);
    ssh_key_free(privkey);

    if (ret != SSH_AUTH_SUCCESS)
    {
        error = NDEV_STATUS::ACCESS_DENIED;
        Debug_printf("NetworkProtocolSFTP::authenticateWithDefaultKey() - failed: %s\r\n", ssh_get_error(session));
        return false;
    }

    return true;
}

bool NetworkProtocolSFTP::sshConnectAndAuth(PeoplesUrlParser *url)
{
    // Repoint base credential pointers at the URL fields (as SSH.cpp does).
    login = &url->user;
    password = &url->password;

    if (login->empty())
    {
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        Debug_printf("NetworkProtocolSFTP::sshConnectAndAuth() - missing username.\r\n");
        return false;
    }

    bool usePasswordAuth = !password->empty();

    if (url->port.empty())
        url->port = "22";

    if (ssh_init() != 0)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::sshConnectAndAuth() - ssh_init failed.\r\n");
        return false;
    }

    session = ssh_new();
    if (session == NULL)
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        Debug_printf("NetworkProtocolSFTP::sshConnectAndAuth() - could not create session.\r\n");
        return false;
    }

    int verbosity = SSH_LOG_PROTOCOL;
    int port = url->getPort();
    ssh_options_set(session, SSH_OPTIONS_USER, login->c_str());
    ssh_options_set(session, SSH_OPTIONS_HOST, url->host.c_str());
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
#ifdef ESP_PLATFORM // apc: access to private member!
    session->opts.config_processed = true;
#endif

    if (ssh_connect(session) != SSH_OK)
    {
        error = NDEV_STATUS::NOT_CONNECTED;
        Debug_printf("NetworkProtocolSFTP::sshConnectAndAuth() - could not connect: %s\r\n", ssh_get_error(session));
        return false;
    }

    // TODO: verify host key against a known list to prevent MITM (see SSH.cpp).

    if (ssh_userauth_none(session, NULL) == SSH_AUTH_ERROR)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::sshConnectAndAuth() - 'none' userauth failed: %s\r\n", ssh_get_error(session));
        return false;
    }

    return usePasswordAuth ? authenticateWithPassword() : authenticateWithDefaultKey();
}

fujiError_t NetworkProtocolSFTP::mount(PeoplesUrlParser *url)
{
    Debug_printf("NetworkProtocolSFTP::mount(%s)\r\n", url->host.c_str());

    if (!sshConnectAndAuth(url))
    {
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    sftp = sftp_new(session);
    if (sftp == nullptr)
    {
        error = NDEV_STATUS::GENERAL;
        Debug_printf("NetworkProtocolSFTP::mount() - sftp_new failed: %s\r\n", ssh_get_error(session));
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    if (sftp_init(sftp) != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        Debug_printf("NetworkProtocolSFTP::mount() - sftp_init failed: %d\r\n", sftp_err);
        umount();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::umount()
{
    if (sftp != nullptr)
    {
        sftp_free(sftp);
        sftp = nullptr;
    }

    if (session != nullptr)
    {
        ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }

    return FUJI_ERROR::NONE;
}

void NetworkProtocolSFTP::fserror_to_error()
{
    switch (sftp_err)
    {
    case SSH_FX_OK:
        error = NDEV_STATUS::SUCCESS;
        break;
    case SSH_FX_EOF:
        error = NDEV_STATUS::END_OF_FILE;
        break;
    case SSH_FX_NO_SUCH_FILE:
    case SSH_FX_NO_SUCH_PATH:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case SSH_FX_PERMISSION_DENIED:
    case SSH_FX_WRITE_PROTECT:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case SSH_FX_FILE_ALREADY_EXISTS:
        error = NDEV_STATUS::FILE_EXISTS;
        break;
    default:
        Debug_printf("SFTP uncaught error: %d\r\n", sftp_err);
        error = NDEV_STATUS::GENERAL;
    }
}

fujiError_t NetworkProtocolSFTP::open_file_handle()
{
    if (sftp == nullptr)
    {
        Debug_printf("NetworkProtocolSFTP::open_file_handle() - no sftp session. aborting.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
    }

    // sftp_open() takes POSIX open() flags and translates them to SSH_FXF_*
    // internally; do NOT pass SSH_FXF_* here (SSH_FXF_READ == O_WRONLY).
    int flags = 0;

    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        flags = O_RDONLY;
        break;
    case ACCESS_MODE::WRITE:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        break;
    case ACCESS_MODE::APPEND:
        flags = O_WRONLY | O_CREAT | O_APPEND;
        break;
    case ACCESS_MODE::READWRITE:
        flags = O_RDWR | O_CREAT;
        break;
    default:
        Debug_printf("NetworkProtocolSFTP::open_file_handle() - Uncaught streamMode %d\r\n", (int)streamMode);
        return FUJI_ERROR::UNSPECIFIED;
    }

    fh = sftp_open(sftp, opened_url->path.c_str(), flags, 0644);
    if (fh == nullptr)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        Debug_printf("NetworkProtocolSFTP::open_file_handle(%s) - error %d\r\n", opened_url->path.c_str(), sftp_err);
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset = 0;
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::open_dir_handle()
{
    dir_handle = sftp_opendir(sftp, dir.c_str());
    if (dir_handle == nullptr)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        Debug_printf("NetworkProtocolSFTP::open_dir_handle(%s) - error %d\r\n", dir.c_str(), sftp_err);
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;

    while (total_len > 0)
    {
        ssize_t actual_len = sftp_read(fh, buf, total_len);

        if (actual_len < 0)
        {
            sftp_err = sftp_get_error(sftp);
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }

        if (actual_len == 0) // EOF - zero-fill the remainder.
        {
            memset(buf, 0, total_len);
            break;
        }

        buf += actual_len;
        total_len -= actual_len;
        offset += actual_len;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::read_dir_entry(char *buf, unsigned short len)
{
    sftp_attributes attr = sftp_readdir(sftp, dir_handle);

    if (attr == nullptr)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    strncpy(buf, attr->name, len);
    buf[len - 1] = '\0';

    fileSize = attr->size;
    mode = attr->permissions;
    is_directory = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY);
    is_locked = !(mode & 0200);

    sftp_attributes_free(attr);

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;

    while (total_len > 0)
    {
        ssize_t actual_len = sftp_write(fh, buf, total_len);

        if (actual_len < 0)
        {
            sftp_err = sftp_get_error(sftp);
            fserror_to_error();
            return FUJI_ERROR::UNSPECIFIED;
        }

        buf += actual_len;
        total_len -= actual_len;
        offset += actual_len;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::close_file_handle()
{
    if (fh != nullptr)
    {
        sftp_close(fh);
        fh = nullptr;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::close_dir_handle()
{
    if (dir_handle != nullptr)
    {
        sftp_closedir(dir_handle);
        dir_handle = nullptr;
    }
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::stat()
{
    sftp_attributes attr = sftp_stat(sftp, opened_url->path.c_str());

    if (attr == nullptr)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    fileSize = attr->size;
    mode = attr->permissions;
    is_directory = (attr->type == SSH_FILEXFER_TYPE_DIRECTORY);
    is_locked = !(mode & 0200);

    sftp_attributes_free(attr);

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::rename(PeoplesUrlParser *url)
{
    if (NetworkProtocolFS::rename(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::NONE;

    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    int rc = sftp_rename(sftp, filename.c_str(), destFilename.c_str());
    if (rc != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
    }

    umount();

    return rc == SSH_OK ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolSFTP::del(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    int rc = sftp_unlink(sftp, url->path.c_str());
    if (rc != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
    }

    umount();

    return rc == SSH_OK ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolSFTP::mkdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    int rc = sftp_mkdir(sftp, url->path.c_str(), 0755);
    if (rc != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
    }

    umount();

    return rc == SSH_OK ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolSFTP::rmdir(PeoplesUrlParser *url)
{
    if (mount(url) != FUJI_ERROR::NONE)
        return FUJI_ERROR::UNSPECIFIED;

    int rc = sftp_rmdir(sftp, url->path.c_str());
    if (rc != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
    }

    umount();

    return rc == SSH_OK ? FUJI_ERROR::NONE : FUJI_ERROR::UNSPECIFIED;
}

fujiError_t NetworkProtocolSFTP::lock(PeoplesUrlParser *url)
{
    if (sftp_chmod(sftp, url->path.c_str(), 0444) != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolSFTP::unlock(PeoplesUrlParser *url)
{
    if (sftp_chmod(sftp, url->path.c_str(), 0644) != SSH_OK)
    {
        sftp_err = sftp_get_error(sftp);
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

off_t NetworkProtocolSFTP::seek(off_t position, int whence)
{
    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();

    if (whence == SEEK_SET)
        offset = position;
    else if (whence == SEEK_CUR)
        offset += position;
    else if (whence == SEEK_END)
        offset = fileSize - position;

    if (sftp_seek64(fh, offset) != 0)
        return -1;

    fileSize -= offset;
    receiveBuffer->clear();

    return offset;
}
