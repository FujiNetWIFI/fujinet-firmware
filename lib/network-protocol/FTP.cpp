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

netProtoErr_t NetworkProtocolFTP::open_file_handle()
{
    netProtoErr_t res;

    switch (aux1_open)
    {
    case PROTOCOL_OPEN_READ:
        stor = false;
        break;
    case PROTOCOL_OPEN_WRITE:
        stor = true;
        break;
    case PROTOCOL_OPEN_APPEND:
    case PROTOCOL_OPEN_READWRITE:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return NETPROTO_ERR_UNSPECIFIED;
        break;
    }

    res = ftp->open_file(opened_url->path, stor);
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::open_dir_handle()
{
    netProtoErr_t res;

    res = ftp->open_directory(dir, filename);
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::mount(PeoplesUrlParser *url)
{
    netProtoErr_t res;

    // Path isn't used
    res = ftp->login("anonymous", "fujinet@fujinet.online", url->host);
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::umount()
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
        error = NETWORK_ERROR_SUCCESS;
        break;
    case 226:
        error = NETWORK_ERROR_END_OF_FILE;
        break;
    case 421:
        error = NETWORK_ERROR_SERVICE_NOT_AVAILABLE;
        break;
    case 400:
    case 425:
        error = NETWORK_ERROR_GENERAL;
        break;
    case 430:
        error = NETWORK_ERROR_INVALID_USERNAME_OR_PASSWORD;
        break;
    case 434:
        error = NETWORK_ERROR_GENERAL;
        break;
    case 450:
    case 451:
    case 452:
        error = NETWORK_ERROR_ACCESS_DENIED;
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
        error = NETWORK_ERROR_GENERAL;
        break;
    case 550:
        error = NETWORK_ERROR_FILE_NOT_FOUND;
        break;
    default:
        error = NETWORK_ERROR_GENERAL;
        break;
    }
}

netProtoErr_t NetworkProtocolFTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    netProtoErr_t res;

    res = ftp->read_file(buf, len);
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::read_dir_entry(char *buf, unsigned short len)
{
    netProtoErr_t res;
    std::string filename;
    long filesz;
    bool is_dir;

    res = ftp->read_directory(filename, filesz, is_dir);
    if (res == false)
    {
        strncpy(buf, filename.c_str(), len);
        fileSize = filesz;
        mode = 0775; // TODO
        is_directory = is_dir;
    }
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::close_file_handle()
{
    netProtoErr_t res;

    res = ftp->close();
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::close_dir_handle()
{
    netProtoErr_t res;

    res = ftp->close();
    fserror_to_error();
    return res;
}

netProtoErr_t NetworkProtocolFTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    return ftp->write_file(buf, len);
}

netProtoErr_t NetworkProtocolFTP::status_file(NetworkStatus *status)
{
    status->rxBytesWaiting = ftp->data_available() > 65535 ? 65535 : ftp->data_available();
    status->connected = ftp->data_connected();
    fserror_to_error();
    status->error = error;

    NetworkProtocol::status(status);
    return NETPROTO_ERR_NONE;
}

uint8_t NetworkProtocolFTP::special_inquiry(uint8_t cmd)
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

netProtoErr_t NetworkProtocolFTP::special_00(cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::rename(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::del(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::mkdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::rmdir(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::lock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::unlock(PeoplesUrlParser *url, cmdFrame_t *cmdFrame)
{
    return NETPROTO_ERR_NONE;
}

netProtoErr_t NetworkProtocolFTP::stat()
{
    return NETPROTO_ERR_NONE;
}
