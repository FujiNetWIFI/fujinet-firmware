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

fujiError_t NetworkProtocolNFS::open_file_handle()
{
    if (nfs == nullptr)
    {
        Debug_printf("NetworkProtocolNFS::open_file_handle() - no nfs context. aborting.\r\n");
        return FUJI_ERROR::UNSPECIFIED;
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

    if (nfs_open(nfs, export_relative(opened_url->path).c_str(), flags, &fh) != 0)
    {
        Debug_printf("NetworkProtocolNFS::open_file_handle() - NFS Error %s\r\n", nfs_get_error(nfs));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset = 0;

    Debug_printf("NetworkProtocolNFS::open_file_handle() - file opened successfully\r\n");

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::open_dir_handle()
{
    if (nfs_opendir(nfs, export_relative(dir).c_str(), &nfs_dir) != 0)
    {
        Debug_printf("NetworkProtocolNFS::open_dir_handle() - ERROR: %s\r\n", nfs_get_error(nfs));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

std::string NetworkProtocolNFS::export_relative(const std::string &path)
{
    // mount_dir keeps its trailing slash, so the remainder is the leaf.
    if (path.size() >= mount_dir.size() &&
        path.compare(0, mount_dir.size(), mount_dir) == 0)
        return "/" + path.substr(mount_dir.size());

    return path.empty() ? "/" : path;
}

fujiError_t NetworkProtocolNFS::mount_path(const std::string &host, const std::string &port, const std::string &path)
{
    // A path ending in '/' names a directory to list; otherwise mount the
    // parent and address the leaf beneath it. mountd lets us mount any
    // directory at or below an export, so this never requires the server
    // to export "/".
    if (!path.empty() && path.back() == '/')
        mount_dir = path;
    else
    {
        size_t slash = path.find_last_of('/');
        mount_dir = (slash == std::string::npos) ? "/" : path.substr(0, slash + 1);
    }

    // The export name mountd matches has no trailing slash (except root).
    std::string exportname = mount_dir;
    if (exportname.size() > 1 && exportname.back() == '/')
        exportname.pop_back();

    Debug_printf("NetworkProtocolNFS::mount() - server: %s port: %s export: %s\r\n",
                 host.c_str(), port.c_str(), exportname.c_str());

    nfs_set_version(nfs, 3);
    nfs_set_auto_traverse_mounts(nfs, 0);
    if (!port.empty())
    {
        int p = atoi(port.c_str());
        nfs_set_mountport(nfs, p);
        nfs_set_nfsport(nfs, p);
    }

    // Set UID/GID from login credentials if provided
    if (login != nullptr)
    {
        nfs_set_uid(nfs, atoi(login->c_str()));
        if (password != nullptr)
            nfs_set_gid(nfs, atoi(password->c_str()));
    }

    nfs_error = nfs_mount(nfs, host.c_str(), exportname.c_str());

    // Kernel NFS exports a subtree (e.g. /public) and refuses MNT "/".
    // rclone/go-nfs exports only "/" and refuses subtree mounts. If the
    // directory mount fails, fall back to the root export and address
    // targets by their absolute path beneath it (export_relative tracks
    // whichever directory actually mounted).
    if (nfs_error != 0 && exportname != "/")
    {
        Debug_printf("NetworkProtocolNFS::mount() - export %s failed (%s), retrying root\r\n",
                     exportname.c_str(), nfs_get_error(nfs));
        mount_dir = "/";
        nfs_error = nfs_mount(nfs, host.c_str(), "/");
    }

    if (nfs_error != 0)
    {
        Debug_printf("NetworkProtocolNFS::mount(%s:%s) - could not mount, NFS error: %s\r\n",
                     host.c_str(), exportname.c_str(), nfs_get_error(nfs));
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::mount(PeoplesUrlParser *url)
{
    return mount_path(url->host, url->port, url->path);
}

fujiError_t NetworkProtocolNFS::umount()
{
    if (nfs == nullptr)
        return FUJI_ERROR::UNSPECIFIED;

    nfs_umount(nfs);
    return FUJI_ERROR::NONE;
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

fujiError_t NetworkProtocolNFS::read_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = nfs_pread(nfs, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::read_dir_entry(char *buf, unsigned short len)
{
    ent = nfs_readdir(nfs, nfs_dir);

    if (ent == nullptr)
    {
        error = NDEV_STATUS::END_OF_FILE;
        return FUJI_ERROR::UNSPECIFIED;
    }

    // Set filename to buffer
    strcpy(buf, ent->name);

    // Get file size/type
    fileSize = ent->size;
    is_directory = S_ISDIR(ent->mode);

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::close_file_handle()
{
    nfs_close(nfs, fh);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::close_dir_handle()
{
    nfs_closedir(nfs, nfs_dir);
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::write_file_handle(uint8_t *buf, unsigned short len)
{
    int actual_len;

    if ((actual_len = nfs_pwrite(nfs, fh, buf, len, offset)) != len)
    {
        fserror_to_error();
        return FUJI_ERROR::UNSPECIFIED;
    }

    offset += actual_len;

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::rename(PeoplesUrlParser *url)
{
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::del(PeoplesUrlParser *url)
{
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::mkdir(PeoplesUrlParser *url)
{
    // Strip trailing slash so the last component is the directory to create.
    std::string path = url->path;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    mount_path(url->host, url->port, path);

    if (nfs_mkdir(nfs, export_relative(path).c_str()) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolNFS::mkdir(%s) NFS error: %s\r\n",url->url.c_str(), nfs_get_error(nfs));
    }

    umount();

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::rmdir(PeoplesUrlParser *url)
{
    // Strip trailing slash so the last component is the directory to remove.
    std::string path = url->path;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    mount_path(url->host, url->port, path);

    if (nfs_rmdir(nfs, export_relative(path).c_str()) != 0)
    {
        fserror_to_error();
        Debug_printf("NetworkProtocolNFS::rmdir(%s) NFS error: %s\r\n",url->url.c_str(), nfs_get_error(nfs));
    }

    umount();

    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::stat()
{
    struct nfs_stat_64 st;

    int ret = nfs_stat64(nfs, export_relative(opened_url->path).c_str(), &st);

    fileSize = st.nfs_size;
    return ret != 0 ? FUJI_ERROR::UNSPECIFIED : FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::lock(PeoplesUrlParser *url)
{
    return FUJI_ERROR::NONE;
}

fujiError_t NetworkProtocolNFS::unlock(PeoplesUrlParser *url)
{
    return FUJI_ERROR::NONE;
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
