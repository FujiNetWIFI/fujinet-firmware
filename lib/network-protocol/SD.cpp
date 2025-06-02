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

bool NetworkProtocolSD::open_file_handle()
{
    const char *mode = FILE_READ;

    // return error if SD is not mounted
    if (check_fs()) return true;

    // Map aux1 to mode
    switch (aux1_open)
    {
    case PROTOCOL_OPEN_READ:
        mode = FILE_READ;
        break;
    case PROTOCOL_OPEN_WRITE:
        mode = FILE_WRITE;
        break;
    case PROTOCOL_OPEN_APPEND:
        mode = FILE_APPEND;
        break;
    case PROTOCOL_OPEN_READWRITE:
        mode = FILE_READ_WRITE;
        break;
    }

    // Do the open.
    if (fh != nullptr)
        close_file_handle();
    fh = fnSDFAT.file_open(opened_url->path.c_str(), mode);
    if (fh == nullptr)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;

    Debug_printf("NetworkProtocolSD::open_file_handle(file: \"%s\" mode: \"%s\") error: %d\r\n",
        opened_url->path.c_str(), mode, error);

    return nullptr == fh;
}

bool NetworkProtocolSD::open_dir_handle()
{
    // return error if SD is not mounted
    if (check_fs()) return true;

    fnSDFAT.dir_close();
    bool success = fnSDFAT.dir_open(dir.c_str(), filename.c_str(), 0);
    if (!success)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;

    Debug_printf("NetworkProtocolSD::open_dir_handle(%s) error: %d\r\n", opened_url->path.c_str(), error);
    return !success;
}

bool NetworkProtocolSD::mount(PeoplesUrlParser *url)
{
    error = NETWORK_ERROR_SUCCESS;
    return check_fs();
}

bool NetworkProtocolSD::check_fs()
{
    if (!fnSDFAT.running()) 
    {
        error = NETWORK_ERROR_CONNECTION_REFUSED;
        return true; // error
    }
    return false; // no error
}

bool NetworkProtocolSD::umount()
{
    error = NETWORK_ERROR_SUCCESS;
    return false; // no error
}

void NetworkProtocolSD::fserror_to_error()
{
    // we keep file system error in NetworkProtocol::error variable, nothing to do here
    Debug_printf("NetworkProtocolSD::fserror_to_error: %d\r\n", error);
}

void NetworkProtocolSD::errno_to_error()
{
    switch(errno)
    {
    case ENOENT:
        error = NETWORK_ERROR_FILE_NOT_FOUND;
        break;
    case EEXIST:
        error = NETWORK_ERROR_FILE_EXISTS;
        break;
    case EACCES:
        error = NETWORK_ERROR_ACCESS_DENIED;
        break;
    case ENOSPC:
        error = NETWORK_ERROR_NO_SPACE_ON_DEVICE;
        break;
    case ENOBUFS:
    case ENOMEM:
        error = NETWORK_ERROR_COULD_NOT_ALLOCATE_BUFFERS;
        break;
    default:
        Debug_printf("NetworkProtocolSD uncaught error: %u\r\n", errno);
        error = NETWORK_ERROR_GENERAL;
        break;
    }
    Debug_printf("NetworkProtocolSD::errno_to_error() %d -> %d\r\n", errno, error);
}

bool NetworkProtocolSD::read_file_handle(uint8_t *buf, unsigned short len)
{
    error = NETWORK_ERROR_SUCCESS;

    if (::fread(buf, 1, len, fh) != len)
    {
        if (feof(fh))
            error = NETWORK_ERROR_END_OF_FILE;
        else
            errno_to_error(); // fread may not set errno!
    }
    Debug_printf("NetworkProtocolSD::read_file_handle(len: %u) error: %d\r\n", len, error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::read_dir_entry(char *buf, unsigned short len)
{
	fsdir_entry_t *entry;
    error = NETWORK_ERROR_SUCCESS;

	entry = fnSDFAT.dir_read();
    if (entry != nullptr)
    {
        strlcpy(buf, entry->filename, len);
        fileSize = entry->size;
        is_directory = entry->isDir;
    }
    else
    {
        error = NETWORK_ERROR_END_OF_FILE;
    }
    Debug_printf("NetworkProtocolSD::read_dir_entry(len: %u) error: %d\r\n", len, error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::close_file_handle()
{
    Debug_printf("NetworkProtocolSD:::close_file_handle()\r\n");
    if (fh != nullptr)
    {
        ::fclose(fh);
        fh = nullptr;
    }
    error = NETWORK_ERROR_SUCCESS;
    return false;
}

bool NetworkProtocolSD::close_dir_handle()
{
    Debug_printf("NetworkProtocolSD:::close_dir_handle()\r\n");
    fnSDFAT.dir_close();
    error = NETWORK_ERROR_SUCCESS;
    return false;
}

bool NetworkProtocolSD::write_file_handle(uint8_t *buf, unsigned short len)
{
    error = NETWORK_ERROR_SUCCESS;

    if (::fwrite(buf, 1, len, fh) != len)
        errno_to_error(); // fwrite may not set errno!
    Debug_printf("NetworkProtocolSD::write_file_handle(len: %u) error: %d\r\n", len, error);

    return NETWORK_ERROR_SUCCESS != error;
}


uint8_t NetworkProtocolSD::special_inquiry(uint8_t cmd)
{
    uint8_t ret;

    switch (cmd)
    {
    case 0x20:      // RENAME
    case 0x21:      // DELETE
    case 0x2A:      // MKDIR
    case 0x2B:      // RMDIR
        ret = 0x80; // Atari to peripheral.
        break;
    default:
        return NetworkProtocolFS::special_inquiry(cmd);
    }

    Debug_printf("NetworkProtocolSD:::special_inquiry(%u) - 0x%02x\r\n", cmd, ret);

    return ret;
}

bool NetworkProtocolSD::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs()) return true;

    if (NetworkProtocolFS::rename(url, cmdFrame) == true)
        return true;

    bool success = fnSDFAT.rename(filename.c_str(), destFilename.c_str());
    if (!success)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;
    Debug_printf("NetworkProtocolSD::rename(%s -> %s) error: %d\r\n", filename.c_str(), destFilename.c_str(), error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs()) return true;

    bool success = fnSDFAT.remove(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;
    Debug_printf("NetworkProtocolSD::del(%s) error: %d\r\n", url->path.c_str(), error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs()) return true;

    bool success = fnSDFAT.mkdir(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;
    Debug_printf("NetworkProtocolSD::mkdir(%s) error: %d\r\n", url->path.c_str(), error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    // return error if SD is not mounted
    if (check_fs()) return true;

    bool success = fnSDFAT.rmdir(url->path.c_str());
    if (!success)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;
    Debug_printf("NetworkProtocolSD::rmdir(%s) error: %d\r\n", url->path.c_str(), error);

    return NETWORK_ERROR_SUCCESS != error;
}

bool NetworkProtocolSD::stat()
{
    // return error if SD is not mounted
    if (check_fs()) return true;

    if (fh != nullptr)
        fileSize = FileSystem::filesize(fh);
    else
        fileSize = fnSDFAT.filesize(opened_url->path.c_str());

    if (fileSize < 0)
        errno_to_error();
    else
        error = NETWORK_ERROR_SUCCESS;

    Debug_printf("NetworkProtocolSD::stat(%s) fileSize: %d\r\n", opened_url->path.c_str(), fileSize);

    return fileSize < 0;
}

bool NetworkProtocolSD::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    Debug_printf("NetworkProtocolSD::lock(%s) - not implemented\r\n", url->path.c_str());

    // return error if SD is not mounted
    if (check_fs()) return true;

    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
}

bool NetworkProtocolSD::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    interruptEnable = false; // no need for network interrupts

    Debug_printf("NetworkProtocolSD::unlock(%s) - not implemented\r\n", url->path.c_str());

    // return error if SD is not mounted
    if (check_fs()) return true;

    error = NETWORK_ERROR_NOT_IMPLEMENTED;
    return true;
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
