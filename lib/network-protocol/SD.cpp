/**
 * NetworkProtocolSD
 *
 * Implementation
 */

#include "SD.h"

#include <errno.h>

#include "../../include/debug.h"

#include "fnFsSD.h"
#include "status_error_codes.h"
#include "compat_string.h"

#include <vector>

NetworkProtocolSD::NetworkProtocolSD(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSD::ctor\r\n");
}

NetworkProtocolSD::~NetworkProtocolSD()
{
    Debug_printf("NetworkProtocolSD::dtor\r\n");
}

protocolError_t NetworkProtocolSD::open_file_handle()
{
    const char *mode = FILE_READ;

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    // Map aux1 to mode
    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        mode = FILE_READ;
        break;
    case ACCESS_MODE::WRITE:
        mode = FILE_WRITE;
        break;
    case ACCESS_MODE::APPEND:
        mode = FILE_APPEND;
        break;
    case ACCESS_MODE::READWRITE:
        mode = FILE_READ_WRITE;
        break;
    default:
        abort();
        break;
    }

    // Do the open.
    if (fh != nullptr)
        close_file_handle();
    fh = fnSDFAT.file_open(opened_url->path.c_str(), mode);
    if (fh == nullptr)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;

    Debug_printf("NetworkProtocolSD::open_file_handle(file: \"%s\" mode: \"%s\") error: %d\r\n",
                 opened_url->path.c_str(), mode, (int) error);

    return nullptr == fh ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::open_dir_handle()
{
    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    fnSDFAT.dir_close();
    bool success = fnSDFAT.dir_open(dir.c_str(), filename.c_str(), 0);
    if (!success)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;

    Debug_printf("NetworkProtocolSD::open_dir_handle(%s) error: %d\r\n", opened_url->path.c_str(), (int) error);
    return success ? PROTOCOL_ERROR::NONE : PROTOCOL_ERROR::UNSPECIFIED;
}

protocolError_t NetworkProtocolSD::mount(PeoplesUrlParser *url)
{
    error = NDEV_STATUS::SUCCESS;
    return check_fs();
}

protocolError_t NetworkProtocolSD::check_fs()
{
    if (!fnSDFAT.running())
    {
        error = NDEV_STATUS::CONNECTION_REFUSED;
        return PROTOCOL_ERROR::UNSPECIFIED; // error
    }
    return PROTOCOL_ERROR::NONE; // no error
}

protocolError_t NetworkProtocolSD::umount()
{
    error = NDEV_STATUS::SUCCESS;
    return PROTOCOL_ERROR::NONE; // no error
}

void NetworkProtocolSD::fserror_to_error()
{
    // we keep file system error in NetworkProtocol::error variable, nothing to do here
    Debug_printf("NetworkProtocolSD::fserror_to_error: %d\r\n", (int) error);
}

void NetworkProtocolSD::errno_to_error()
{
    switch(errno)
    {
    case ENOENT:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    case EEXIST:
        error = NDEV_STATUS::FILE_EXISTS;
        break;
    case EACCES:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case ENOSPC:
        error = NDEV_STATUS::NO_SPACE_ON_DEVICE;
        break;
    case ENOBUFS:
    case ENOMEM:
        error = NDEV_STATUS::COULD_NOT_ALLOCATE_BUFFERS;
        break;
    default:
        Debug_printf("NetworkProtocolSD uncaught error: %u\r\n", errno);
        error = NDEV_STATUS::GENERAL;
        break;
    }
    Debug_printf("NetworkProtocolSD::errno_to_error() %d -> %d\r\n", errno, (int) error);
}

protocolError_t NetworkProtocolSD::read_file_handle(uint8_t *buf, unsigned short len)
{
    error = NDEV_STATUS::SUCCESS;

    if (::fread(buf, 1, len, fh) != len)
    {
        if (feof(fh))
            error = NDEV_STATUS::END_OF_FILE;
        else
            errno_to_error(); // fread may not set errno!
    }
    Debug_printf("NetworkProtocolSD::read_file_handle(len: %u) error: %d\r\n", len, (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::read_dir_entry(char *buf, unsigned short len)
{
fsdir_entry_t *entry;
    error = NDEV_STATUS::SUCCESS;

entry = fnSDFAT.dir_read();
    if (entry != nullptr)
    {
        strlcpy(buf, entry->filename, len);
        fileSize = entry->size;
        is_directory = entry->isDir;
    }
    else
    {
        error = NDEV_STATUS::END_OF_FILE;
    }
    Debug_printf("NetworkProtocolSD::read_dir_entry(len: %u) error: %d\r\n", len, (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::close_file_handle()
{
    Debug_printf("NetworkProtocolSD:::close_file_handle()\r\n");
    if (fh != nullptr)
    {
        ::fclose(fh);
        fh = nullptr;
    }
    error = NDEV_STATUS::SUCCESS;
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::close_dir_handle()
{
    Debug_printf("NetworkProtocolSD:::close_dir_handle()\r\n");
    fnSDFAT.dir_close();
    error = NDEV_STATUS::SUCCESS;
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::write_file_handle(uint8_t *buf, unsigned short len)
{
    error = NDEV_STATUS::SUCCESS;

    if (::fwrite(buf, 1, len, fh) != len)
        errno_to_error(); // fwrite may not set errno!
    Debug_printf("NetworkProtocolSD::write_file_handle(len: %u) error: %d\r\n", len, (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}


AtariSIODirection NetworkProtocolSD::special_inquiry(fujiCommandID_t cmd)
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

    Debug_printf("NetworkProtocolSD:::special_inquiry(%u) - 0x%02x\r\n", cmd, ret);

    return ret;
}

protocolError_t NetworkProtocolSD::special_00(cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    if (NetworkProtocolFS::rename(url, cmdFrame) != PROTOCOL_ERROR::NONE)
        return PROTOCOL_ERROR::UNSPECIFIED;

    bool success = fnSDFAT.rename(filename.c_str(), destFilename.c_str());
    if (!success)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;
    Debug_printf("NetworkProtocolSD::rename(%s -> %s) error: %d\r\n", filename.c_str(), destFilename.c_str(), (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    bool success = fnSDFAT.remove(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;
    Debug_printf("NetworkProtocolSD::del(%s) error: %d\r\n", url->path.c_str(), (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    bool success = fnSDFAT.mkdir(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;
    Debug_printf("NetworkProtocolSD::mkdir(%s) error: %d\r\n", url->path.c_str(), (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    bool success = fnSDFAT.rmdir(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;
    Debug_printf("NetworkProtocolSD::rmdir(%s) error: %d\r\n", url->path.c_str(), (int) error);

    return NDEV_STATUS::SUCCESS != error ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::stat()
{
    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    if (fh != nullptr)
        fileSize = FileSystem::filesize(fh);
    else
        fileSize = fnSDFAT.filesize(opened_url->path.c_str());

    if (fileSize < 0)
        errno_to_error();
    else
        error = NDEV_STATUS::SUCCESS;

    Debug_printf("NetworkProtocolSD::stat(%s) fileSize: %d\r\n", opened_url->path.c_str(), fileSize);

    return fileSize < 0 ? PROTOCOL_ERROR::UNSPECIFIED : PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolSD::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    Debug_printf("NetworkProtocolSD::lock(%s) - not implemented\r\n", url->path.c_str());

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    error = NDEV_STATUS::NOT_IMPLEMENTED;
    return PROTOCOL_ERROR::UNSPECIFIED;
}

protocolError_t NetworkProtocolSD::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    Debug_printf("NetworkProtocolSD::unlock(%s) - not implemented\r\n", url->path.c_str());

    // return error if SD is not mounted
    if (check_fs() != PROTOCOL_ERROR::NONE) return PROTOCOL_ERROR::UNSPECIFIED;

    error = NDEV_STATUS::NOT_IMPLEMENTED;
    return PROTOCOL_ERROR::UNSPECIFIED;
}

off_t NetworkProtocolSD::seek(off_t offset, int whence)
{
    off_t new_offset;


    new_offset = ::fseek(fh, offset, whence);

    // fileSize isn't fileSize, it's bytes remaining. Call stat() to fix fileSize
    stat();
    fileSize -= new_offset;
    receiveBuffer->clear();

    return new_offset;
}
