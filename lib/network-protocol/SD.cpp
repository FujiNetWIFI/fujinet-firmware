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

    if (fnSDFAT.running() == false)
    {
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    // Map aux1 to mode
    switch (aux1_open)
    {
    case 4:
        mode = FILE_READ;
        break;
    case 8:
        mode = FILE_WRITE;
        break;
    case 9:
        mode = FILE_APPEND;
        break;
    case 12:
        mode = FILE_READ_WRITE;
        break;
    }

    // Do the open.
    close_file_handle();
    fh = fnSDFAT.file_open(opened_url->path.c_str(), mode);
    if (fh == nullptr)
        fserror_to_error();

    Debug_printf("NetworkProtocolSD::open_file_handle(file: \"%s\" mode: \"%s\") - %d\r\n",
        opened_url->path.c_str(), mode, error);

    return fh == nullptr;
}

bool NetworkProtocolSD::open_dir_handle()
{
    if (fnSDFAT.running() == false)
    {
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    fnSDFAT.dir_close();
    error = NETWORK_ERROR_SUCCESS;
    bool success = fnSDFAT.dir_open(dir.c_str(), filename.c_str(), 0);
    if (!success)
        fserror_to_error();

    Debug_printf("NetworkProtocolSD::open_dir_handle(%s) - %d\r\n", opened_url->path.c_str(), error);
    return !success;
}

bool NetworkProtocolSD::mount(PeoplesUrlParser *url)
{
    return !fnSDFAT.running();
}

bool NetworkProtocolSD::umount()
{
    return false; // always success.
}

void NetworkProtocolSD::fserror_to_error()
{
    switch(errno)
    {
    // TODO
    default:
        Debug_printf("NetworkProtocolSD uncaught error: %u\r\n", errno);
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

bool NetworkProtocolSD::read_file_handle(uint8_t *buf, unsigned short len)
{
    Debug_printf("NetworkProtocolSD::read_file_handle - len %u\r\n",len);
    error = NETWORK_ERROR_SUCCESS;
    if (::fread(buf, 1, len, fh) != len)
    {
        fserror_to_error();
        return true;
    }

    return false;
}

bool NetworkProtocolSD::read_dir_entry(char *buf, unsigned short len)
{
	fsdir_entry_t *entry;

    Debug_printf("NetworkProtocolSD::read_dir_entry - len %u\r\n",len);

    error = NETWORK_ERROR_SUCCESS;
	entry = fnSDFAT.dir_read();
    if (entry == nullptr)
    {
        error = NETWORK_ERROR_END_OF_FILE;
        return true;
    }

    strlcpy(buf, entry->filename, len);
	fileSize = entry->size;
    is_directory = entry->isDir;
    return false;
}

bool NetworkProtocolSD::close_file_handle()
{
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
    fnSDFAT.dir_close();
    error = NETWORK_ERROR_SUCCESS;
    return false;
}

bool NetworkProtocolSD::write_file_handle(uint8_t *buf, unsigned short len)
{
    error = NETWORK_ERROR_SUCCESS;
    if (::fwrite(buf, 1, len, fh) != len)
    {
        fserror_to_error();
        return true;
    }

    return false;
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
    return false;
}

bool NetworkProtocolSD::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolSD::del(%s)\r\n", url->path.c_str());

    if (fnSDFAT.running() == false)
    {
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    error = NETWORK_ERROR_SUCCESS;
    bool ok = fnSDFAT.remove(url->path.c_str());
    if (!ok)
        fserror_to_error();
    return !ok;
}

bool NetworkProtocolSD::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolSD::mkdir(%s)\r\n", url->path.c_str());

    if (fnSDFAT.running() == false)
    {
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    error = NETWORK_ERROR_SUCCESS;
    bool ok = fnSDFAT.mkdir(url->path.c_str());
    if (!ok)
        fserror_to_error();
    return !ok;
}

bool NetworkProtocolSD::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    Debug_printf("NetworkProtocolSD::rmdir(%s)\r\n", url->path.c_str());

    if (fnSDFAT.running() == false)
    {
        error = NETWORK_ERROR_GENERAL;
        return true;
    }

    error = NETWORK_ERROR_SUCCESS;
    bool ok = fnSDFAT.rmdir(url->path.c_str());
    if (!ok)
        fserror_to_error();
    return !ok;
}

bool NetworkProtocolSD::stat()
{
    if (fh != nullptr)
        fileSize = FileSystem::filesize(fh);
    else
        fileSize = fnSDFAT.filesize(opened_url->path.c_str());
    Debug_printf("NetworkProtocolSD::stat - fileSize: %d\r\n", fileSize);
    return fileSize < 0;
}

bool NetworkProtocolSD::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    error = NETWORK_ERROR_GENERAL;
    return true;
}

bool NetworkProtocolSD::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    error = NETWORK_ERROR_GENERAL;
    return true;
}
