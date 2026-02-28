/**
 * NetworkProtocolNFS
 *
 * Implementation
 */

#include "NFS.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <cstring>

#include "../../include/debug.h"

#include <nfsc/libnfs.h>

#include "status_error_codes.h"
#include "utils.h"

#include <vector>

NetworkProtocolNFS::NetworkProtocolNFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolNFS::ctor\r\n");
    nfs = nfs_init_context();
}

NetworkProtocolNFS::~NetworkProtocolNFS()
{
    Debug_printf("NetworkProtocolNFS::dtor\r\n");
    nfs_destroy_context(nfs);
}

protocolError_t NetworkProtocolNFS::open_file_handle()
{
    if (nfs == nullptr)
    {
        Debug_printf("NetworkProtocolNFS::open_file_handle() - no nfs context. aborting.\r\n");
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    // Determine flags
    int flags = 0;

    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        flags = O_RDONLY;
        break;
    case ACCESS_MODE::WRITE:
        flags = O_WRONLY | O_CREAT;
        break;
    case ACCESS_MODE::APPEND:
        flags = O_APPEND | O_CREAT;
        break;
    case ACCESS_MODE::READWRITE:
        flags = O_RDWR;
        break;
    default:
        Debug_printf("NetworkProtocolNFS::open_file_handle() - Uncaught aux1 %d", (int) streamMode);
    }

    if (nfs_open(nfs, nfs_url->path, flags, &fh) != 0)
    {
        Debug_printf("NetworkProtocolNFS::open_file_handle() - NFS Error %s\r\n", nfs_get_error(nfs));
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    offset = 0;

    Debug_printf("NetworkProtocolNFS::open_file_handle() - file opened successfully\r\n");

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::open_dir_handle()
{
    if (nfs_opendir(nfs, nfs_url->path, &nfs_dir) != 0)
    {
        Debug_printf("NetworkProtocolNFS::open_dir_handle() - ERROR: %s\r\n", nfs_get_error(nfs));
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::mount(PeoplesUrlParser *url)
{
    std::string openURL = url->url;

    // use mRawURL to bypass our normal URL processing.
    if (openURL.find("NFS:") != std::string::npos)
    {
        openURL[0] = 'n';
        openURL[1] = 'f';
        openURL[2] = 's';
    }

    Debug_printf("NetworkProtocolNFS::mount() - openURL: %s\r\n", openURL.c_str());
    nfs_url = nfs_parse_url_full(nfs, openURL.c_str());
    if (nfs_url == nullptr)
    {
        Debug_printf("NetworkProtocolNFS::mount(%s) - failed to parse URL, NFS error: %s\n", openURL.c_str(), nfs_get_error(nfs));
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    // Set UID/GID from login credentials if provided
    if (login != nullptr)
    {
        nfs_set_uid(nfs, atoi(login->c_str()));
        if (password != nullptr)
            nfs_set_gid(nfs, atoi(password->c_str()));
    }

    if ((nfs_error = nfs_mount(nfs, nfs_url->server, nfs_url->path)) != 0)
    {
        Debug_printf("NetworkProtocolNFS::mount(%s) - could not mount, NFS error: %s\r\n", openURL.c_str(), nfs_get_error(nfs));
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::umount()
{
    if (nfs == nullptr)
        return PROTOCOL_ERROR::UNSPECIFIED;

    nfs_umount(nfs);

    if (nfs_url == nullptr)
        return PROTOCOL_ERROR::UNSPECIFIED;

    nfs_destroy_url(nfs_url);
    return PROTOCOL_ERROR::NONE;
}

void NetworkProtocolNFS::fserror_to_error()
{
    switch (nfs_error)
    {
    default:
        error = NDEV_STATUS::GENERAL;
        break;
    }
}

protocolError_t NetworkProtocolNFS::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = nfs_pread(nfs, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::read_dir_entry(char *buf, unsigned short len)
{
    ent = nfs_readdir(nfs, nfs_dir);

    if (ent == nullptr)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    // Set filename to buffer
    strcpy(buf, ent->name);

    // Get file size/type
    fileSize = ent->size;
    is_directory = S_ISDIR(ent->mode);

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::close_file_handle()
{
    nfs_close(nfs, fh);
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::close_dir_handle()
{
    nfs_closedir(nfs, nfs_dir);
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = nfs_pwrite(nfs, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return PROTOCOL_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::rename(PeoplesUrlParser *url)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::del(PeoplesUrlParser *url)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::mkdir(PeoplesUrlParser *url)
{
    mount(url);

    if (nfs_mkdir(nfs, nfs_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolNFS::mkdir(%s) NFS error: %s\r\n",url->url.c_str(), nfs_get_error(nfs));
    }

    umount();

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::rmdir(PeoplesUrlParser *url)
{
    mount(url);

    if (nfs_rmdir(nfs, nfs_url->path) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolNFS::rmdir(%s) NFS error: %s\r\n",url->url.c_str(), nfs_get_error(nfs));
    }

    umount();

    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::stat()
{
    struct nfs_stat_64 st;

    int ret = nfs_stat64(nfs, nfs_url->path, &st);

    fileSize = st.nfs_size;
    return ret != 0 ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::lock(PeoplesUrlParser *url)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolNFS::unlock(PeoplesUrlParser *url)
{
    return PROTOCOL_ERROR::NONE;
}

off_t NetworkProtocolNFS::seek(off_t position, int whence)
{
    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();

    if (whence == SEEK_SET)
        offset = position;
    else if (whence == SEEK_CUR)
        offset += position;
    else if (whence == SEEK_END)
        offset = fileSize - position;

    fileSize -= offset;
    receiveBuffer->clear();

    return offset;
}
