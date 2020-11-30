/**
 * NetworkProtocolTNFS
 * 
 * Implementation
 */

#include "TNFS.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolTNFS::NetworkProtocolTNFS(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
}

NetworkProtocolTNFS::~NetworkProtocolTNFS()
{
}

bool NetworkProtocolTNFS::open_file_handle()
{
    // Map aux1 to mode and perms for tnfs_open()
    switch (aux1_open)
    {
    case 4:
        mode = TNFS_OPENMODE_READ;
        perms = 0;
        break;
    case 8:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE_TRUNCATE | TNFS_OPENMODE_WRITE;
        perms = 0x1FF;
        break;
    case 9:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_WRITE | TNFS_OPENMODE_WRITE_APPEND; // 0x10B
        perms = 0x1FF;
        break;
    case 12:
        mode = TNFS_OPENMODE_WRITE_CREATE | TNFS_OPENMODE_READWRITE;
        perms = 0x1FF;
        break;
    }

    // Do the open.
    tnfs_error = tnfs_open(&mountInfo, path.c_str(), mode, perms, &fd);
    fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::open_dir_handle()
{
    tnfs_error = tnfs_opendirx(&mountInfo, dir.c_str(), 0, 0, filename.c_str(), 0);
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::mount(string hostName, string path)
{
    Debug_printf("NetworkProtocolTNFS::mount(%s,%s)\n", hostName.c_str(), path.c_str());
    strcpy(mountInfo.hostname, hostName.c_str());
    strcpy(mountInfo.mountpath, "/");

    tnfs_error = tnfs_mount(&mountInfo);
    fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::umount()
{
    Debug_printf("NetworkProtocolTNFS::umount()\n");
    tnfs_umount(&mountInfo);

    return false; // always success.
}

void NetworkProtocolTNFS::fserror_to_error()
{
    switch (tnfs_error)
    {
    case -1: // special case for mount
        error = NETWORK_ERROR_GENERAL_TIMEOUT;
        break;
    case TNFS_RESULT_SUCCESS:
        error = NETWORK_ERROR_SUCCESS;
        break;
    case TNFS_RESULT_FILE_NOT_FOUND:
        error = NETWORK_ERROR_FILE_NOT_FOUND;
        break;
    case TNFS_RESULT_READONLY_FILESYSTEM:
    case TNFS_RESULT_ACCESS_DENIED:
        error = NETWORK_ERROR_ACCESS_DENIED;
        break;
    case TNFS_RESULT_NO_SPACE_ON_DEVICE:
        error = NETWORK_ERROR_NO_SPACE_ON_DEVICE;
        break;
    case TNFS_RESULT_END_OF_FILE:
        error = NETWORK_ERROR_END_OF_FILE;
        break;
    default:
        Debug_printf("TNFS uncaught error: %u\n", tnfs_error);
        error = NETWORK_ERROR_GENERAL;
    }
}

bool NetworkProtocolTNFS::read_file_handle(uint8_t *buf, unsigned short len)
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

        tnfs_error = tnfs_read(&mountInfo, fd, buf, block_len, &actual_len);
        if (tnfs_error != 0)
        {
            fserror_to_error();
            return true; // error.
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }
    }

    fserror_to_error();
    return false; // no error
}

bool NetworkProtocolTNFS::read_dir(unsigned short len)
{
    if (receiveBuffer->length() == 0)
    {
        *receiveBuffer = dirBuffer.substr(0, len);
        dirBuffer.erase(0, len);
    }

    return NetworkProtocol::read(len);
}

bool NetworkProtocolTNFS::read_dir_entry(char *buf, unsigned short len)
{
    tnfs_error = tnfs_readdirx(&mountInfo, &fileStat, buf, len);
    fileSize = fileStat.filesize;
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::close_file_handle()
{
    Debug_printf("NetworkProtocolTNFS::close_file_handle(%u)\n", fd);
    if (fd != 0)
        tnfs_error = tnfs_close(&mountInfo, fd);
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::close_dir_handle()
{
    tnfs_error = tnfs_closedir(&mountInfo);
    fserror_to_error();
    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::write_file_handle(uint8_t *buf, unsigned short len)
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

        if (tnfs_write(&mountInfo, fd, buf, block_len, &actual_len) != 0)
        {
            return true; // error.
        }
        else
        {
            buf += block_len;
            total_len -= block_len;
        }
    }
    return false; // no error
}

uint8_t NetworkProtocolTNFS::special_inquiry(uint8_t cmd)
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

    return ret;
}

bool NetworkProtocolTNFS::special_00(cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_00(cmdFrame);
    }
}

bool NetworkProtocolTNFS::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    switch (cmdFrame->comnd)
    {
    default:
        return NetworkProtocolFS::special_40(sp_buf, len, cmdFrame);
    }
}

bool NetworkProtocolTNFS::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolTNFS::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::rename(url, cmdFrame) == true)
        return true;

    mount(url->hostName, url->path);
    
    tnfs_error = tnfs_rename(&mountInfo, filename.c_str(), destFilename.c_str());
    
    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();
    
    umount();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::del(url, cmdFrame) == true)
        return true;

    mount(url->hostName, url->path);

    tnfs_error = tnfs_unlink(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    umount();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::mkdir(url, cmdFrame) == true)
        return true;

    mount(url->hostName, url->path);

    tnfs_error = tnfs_mkdir(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    if (NetworkProtocolFS::rmdir(url, cmdFrame) == true)
        return true;

    mount(url->hostName, url->path);

    tnfs_error = tnfs_rmdir(&mountInfo, url->path.c_str());

    if (tnfs_error != TNFS_RESULT_SUCCESS)
        fserror_to_error();

    return tnfs_error != TNFS_RESULT_SUCCESS;
}

bool NetworkProtocolTNFS::stat(string path)
{
    tnfs_error = tnfs_stat(&mountInfo, &fileStat, path.c_str());
    fileSize = fileStat.filesize;
    return tnfs_error != TNFS_RESULT_SUCCESS;
}