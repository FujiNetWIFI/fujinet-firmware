/**
 * NetworkProtocolFTP
 *
 * Implementation
 */

#include "FTP.h"

#include <cstring>

#include "../../include/debug.h"

#include "status_error_codes.h"

#include <vector>


NetworkProtocolFTP::NetworkProtocolFTP(std::string *rx_buf, std::string *tx_buf, std::string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    Debug_printf("NetworkProtocolFTP::ctor\r\n");
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    ftp = new fnFTP();
}

NetworkProtocolFTP::~NetworkProtocolFTP()
{
    Debug_printf("NetworkProtocolFTP::dtor\r\n");
    delete ftp;
    ftp = nullptr;
}

protocolError_t NetworkProtocolFTP::open_file_handle()
{
    protocolError_t res;

    switch (streamMode)
    {
    case ACCESS_MODE::READ:
        stor = false;
        break;
    case ACCESS_MODE::WRITE:
        stor = true;
        break;
    case ACCESS_MODE::APPEND:
    case ACCESS_MODE::READWRITE:
        error = NDEV_STATUS::NOT_IMPLEMENTED;
        return PROTOCOL_ERROR::UNSPECIFIED;
    default:
        break;
    }

    res = ftp->open_file(opened_url->path, stor);
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::open_dir_handle()
{
    protocolError_t res;

    res = ftp->open_directory(dir, filename);
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::mount(PeoplesUrlParser *url)
{
    protocolError_t res;

    // Path isn't used
    res = ftp->login("anonymous", "fujinet@fujinet.online", url->host);
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::umount()
{
    return ftp->logout();
}

void NetworkProtocolFTP::fserror_to_error()
{
    switch (ftp->status())
    {
    case 110:
    case 120:
    case 125:
    case 150:
    case 200:
    case 202:
    case 211:
    case 212:
    case 213:
    case 214:
    case 215:
    case 220:
    case 221:
    case 225:
    case 227:
    case 228:
    case 229:
    case 230:
    case 231:
    case 232:
    case 234:
    case 250:
    case 257:
    case 300:
    case 331:
    case 332:
    case 350:
        error = NDEV_STATUS::SUCCESS;
        break;
    case 226:
        error = NDEV_STATUS::END_OF_FILE;
        break;
    case 421:
        error = NDEV_STATUS::SERVICE_NOT_AVAILABLE;
        break;
    case 400:
    case 425:
        error = NDEV_STATUS::GENERAL;
        break;
    case 430:
        error = NDEV_STATUS::INVALID_USERNAME_OR_PASSWORD;
        break;
    case 434:
        error = NDEV_STATUS::GENERAL;
        break;
    case 450:
    case 451:
    case 452:
        error = NDEV_STATUS::ACCESS_DENIED;
        break;
    case 500:
    case 501:
    case 502:
    case 503:
    case 504:
    case 530:
    case 532:
    case 534:
    case 551:
    case 552:
    case 553:
        error = NDEV_STATUS::GENERAL;
        break;
    case 550:
        error = NDEV_STATUS::FILE_NOT_FOUND;
        break;
    default:
        error = NDEV_STATUS::GENERAL;
        break;
    }
}

protocolError_t NetworkProtocolFTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    protocolError_t res;

    res = ftp->read_file(buf, len);
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::read_dir_entry(char *buf, unsigned short len)
{
    protocolError_t res;
    std::string filename;
    long filesz;
    bool is_dir;

    res = ftp->read_directory(filename, filesz, is_dir);
    if (res == PROTOCOL_ERROR::NONE)
    {
        strncpy(buf, filename.c_str(), len);
        fileSize = filesz;
        mode = 0775; // TODO
        is_directory = is_dir;
    }
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::close_file_handle()
{
    protocolError_t res;

    res = ftp->close();
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::close_dir_handle()
{
    protocolError_t res;

    res = ftp->close();
    fserror_to_error();
    return res;
}

protocolError_t NetworkProtocolFTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    return ftp->write_file(buf, len);
}

protocolError_t NetworkProtocolFTP::status_file(NetworkStatus *status)
{
    status->connected = ftp->data_connected() != PROTOCOL_ERROR::NONE;
    fserror_to_error();
    status->error = error;

    NetworkProtocol::status(status);
    return PROTOCOL_ERROR::NONE;
}

AtariSIODirection NetworkProtocolFTP::special_inquiry(fujiCommandID_t cmd)
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

    return ret;
}

protocolError_t NetworkProtocolFTP::special_00(cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return PROTOCOL_ERROR::NONE;
}

protocolError_t NetworkProtocolFTP::stat()
{
    return PROTOCOL_ERROR::NONE;
}

size_t NetworkProtocolFTP::available()
{
    size_t avail = 0;


    switch (streamType)
    {
    case streamType_t::FILE:
        avail = ftp->data_available();
        break;
    case DIR:
        avail = receiveBuffer->length();
        if (!avail)
            avail = dirBuffer.length();
        break;
    default:
        abort();
        break;
    }

    return avail;
}
