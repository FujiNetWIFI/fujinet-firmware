/**
 * NetworkProtocolTNFS
 *
 * Implementation
 */

#include "TNFS.h"

#include <string.h>

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <vector>


NetworkProtocolTNFS::NetworkProtocolTNFS(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolTNFS::ctor\r\n");
}

NetworkProtocolTNFS::~NetworkProtocolTNFS()
{
    Debug_printf("NetworkProtocolTNFS::dtor\r\n");
}

protocolError_t NetworkProtocolTNFS::open_file_handle()
{
    // Map aux1 to mode and perms for tnfs_open()
    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        mode = TNFS_OPENMODE_READ;
        perms = 0;
        break;
    case ACCESS_MODE::WRITE:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE_TRUNCATE | TNFS_OPENMODE_WRITE;
        perms = 0x1FF;
        break;
    case ACCESS_MODE::APPEND:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_APPEND; // 0x10B
        perms = 0x1FF;
        break;
    case ACCESS_MODE::READWRITE:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_READWRITE;
        perms = 0x1FF;
        break;
    default:
        abort();
        break;
    }

    // Do the open.
    tnfs_error = tnfs_open(&mountInfo, opened_url->path.c_str(), mode, perms, &fd);
    fserror_to_error();

    Debug_printf("NetworkProtocolTNFS::open_file_handle(mode: %d perms %d) - %d\r\n", mode, perms, tnfs_error);

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::open_dir_handle()
{
    tnfs_error = tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, filename.c_str(), 0);
    fserror_to_error();

    Debug_printf("NetworkProtocolTNFS::open_dir_handle(%s, %s) - %d\r\n", dir.c_str(), filename.c_str(), tnfs_error);

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::mount(PeoplesUrlParser *url)
{
    strcpy(mountInfo.hostname, url->host.c_str());
    strcpy(mountInfo.mountpath, "/");

    tnfs_error = tnfs_mount(&mountInfo);
    fserror_to_error();

    Debug_printf("NetworkProtocolTNFS::mount(%s,%s) - %d\r\n", url->host.c_str(), url->path.c_str(), tnfs_error);

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::umount()
{
    Debug_printf("NetworkProtocolTNFS::umount()\r\n");
    tnfs_umount(&mountInfo);

    return PROTOCOL_ERROR::NONE; // always success.
}

void NetworkProtocolTNFS::fserror_to_error()
{
    switch (tnfs_error)
    {
    case -1: // special case for mount
        error = NDEV_STATUS::GENERAL_TIMEOUT;
        break;
    case TNFS_RESULT_SUCCESS:
        error = NDEV_STATUS::SUCCESS;
        break;
    case TNFS_RESULT_FILE_NOT_FOUND:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case TNFS_RESULT_READONLY_FILESYSTEM:
    case TNFS_RESULT_ACCESS_DENIED:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        break;
    case TNFS_RESULT_END_OF_FILE:
        error = NDEV_STATUS::END_OF_FILE;
        break;
    case TNFS_RESULT_FILE_EXISTS:
        error = NDEV_STATUS::FILE_EXISTS;
        break;
    default:
        Debug_printf("TNFS uncaught error: %u\r\n", tnfs_error);
        error = NDEV_STATUS::GENERAL;
    }
}

protocolError_t NetworkProtocolTNFS::read_file_handle(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        //Debug_printf("NetworkProtocolTNFS::read_file_handle - read block size %u\r\n",block_len);

        tnfs_error = tnfs_read(&mountInfo, fd, buf, block_len, &actual_len);
        if (tnfs_error != 0)
        {
            fserror_to_error();
            return PROTOCOL_ERROR::UNSPECIFIED; // error.
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }
    }

    Debug_printf("NetworkProtocolTNFS::read_file_handle(B: %d, A: %d, T: %d)\r\n", block_len, actual_len, total_len);

    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::read_dir_entry(char *buf, unsigned short len)
{
    tnfs_error = tnfs_readdirx(&mountInfo, &fileStat, buf, len);
    fileSize = fileStat.filesize;
    mode = fileStat.mode;
    is_directory = fileStat.isDir;
    is_locked = (fileStat.mode & 0200);
    fserror_to_error();
    Debug_printf("NetworkProtocolTNFS::read_dir_entry(N: %s, F: %d, M: %d, D: %d, L: %d) - %d\r\n", buf, fileSize, mode, is_directory, is_locked, tnfs_error);
    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::close_file_handle()
{
    if (fd != 0)
        tnfs_error = tnfs_close(&mountInfo, fd);
    fserror_to_error();
    Debug_printf("NetworkProtocolTNFS::close_file_handle(%u) - %d\r\n", fd, tnfs_error);
    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::close_dir_handle()
{
    tnfs_error = tnfs_closedir(&mountInfo);
    fserror_to_error();
    Debug_printf("NetworkProtocolTNFS::close_dir_handle() - %d\r\n", tnfs_error);
    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::write_file_handle(uint8_t *buf, unsigned short len)
{
    unsigned short total_len = len;
    unsigned short block_len = TNFS_MAX_READWRITE_PAYLOAD;
    uint16_t actual_len;

    while (total_len > 0)
    {
        if (total_len > TNFS_MAX_READWRITE_PAYLOAD)
            block_len = TNFS_MAX_READWRITE_PAYLOAD;
        else
            block_len = total_len;

        if ((tnfs_error = tnfs_write(&mountInfo, fd, buf, block_len, &actual_len)) != 0)
        {
            fserror_to_error();
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }

        Debug_printf("NetworkProtocolTNFS::write_file_handle(B: %d, A: %d, T: %d)\r\n", block_len, actual_len, total_len);
    }

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

AtariSIODirection NetworkProtocolTNFS::special_inquiry(fujiCommandID_t cmd)
{
    AtariSIODirection ret;

    switch (cmd)
    {
    case NETCMD_RENAME:
    case NETCMD_DELETE:
    case NETCMD_MKDIR:
    case NETCMD_RMDIR:
        ret = SIO_DIRECTION_WRITE; // Atari to peripheral.
        break;
    default:
        return NetworkProtocolFS::special_inquiry(cmd);
    }

    Debug_printf("NetworkProtocolTNFS:::special_inquiry(%u) - 0x%02x\r\n", cmd, ret);

    return ret;
}

protocolError_t NetworkProtocolTNFS::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_00(cmdFrame);
    }
}

protocolError_t NetworkProtocolTNFS::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_40(sp_buf, len, cmdFrame);
    }
}

protocolError_t NetworkProtocolTNFS::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::rename(url, cmdFrame) != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::NONE;

    mount(url);

    tnfs_error = tnfs_rename(&mountInfo, filename.c_str(), destFilename.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    umount();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    tnfs_error = tnfs_unlink(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    umount();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolTNFS::mkdir(%s,%s)", url->host.c_str(), url->path.c_str());

    mount(url);

    tnfs_error = tnfs_mkdir(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    mount(url);

    tnfs_error = tnfs_rmdir(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::stat()
{
    tnfs_error = tnfs_stat(&mountInfo, &fileStat, opened_url->path.c_str());
    fileSize = fileStat.filesize;
    mode = fileStat.mode;
    is_locked = (mode & 0200);

    Debug_printf("NetworkProtocolTNFS::stat(F: %d, M: %d, D: %d, L: %d) - %d\r\n", fileSize, mode, is_directory, is_locked, tnfs_error);

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("lock: %s\r\n", url->path.c_str());
    tnfs_error = tnfs_chmod(&mountInfo, url->path.c_str(), 0444);

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolTNFS::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    tnfs_error = tnfs_chmod(&mountInfo, url->path.c_str(), 0644);

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

off_t NetworkProtocolTNFS::seek(off_t offset, int whence)
{
    int err;
    uint32_t new_offset;


    err = tnfs_lseek(&mountInfo, fd, offset, whence, &new_offset, false);
    if (err)
        return -1;

    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();
    fileSize -= new_offset;
    receiveBuffer->clear();
    return new_offset;
}
