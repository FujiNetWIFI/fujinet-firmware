/**
 * NetworkProtocolFTP
 * 
 * Implementation
 */

#include <cstring>
#include "FTP.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolFTP::NetworkProtocolFTP(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    ftp = new fnFTP();
}

NetworkProtocolFTP::~NetworkProtocolFTP()
{
    delete ftp;
}

bool NetworkProtocolFTP::open_file_handle()
{
    bool res;

    switch (aux1_open)
    {
    case 4:
        stor = false;
        break;
    case 8:
        stor = true;
        break;
    case 9:
    case 12:
        error = NETWORK_ERROR_NOT_IMPLEMENTED;
        return true;
        break;
    }

    res = ftp->open_file(opened_url->path, stor);
    fserror_to_error();
    return res;
}

bool NetworkProtocolFTP::open_dir_handle()
{
    bool res;

    res = ftp->open_directory(opened_url->path, filename);
    fserror_to_error();
    return res;
}

bool NetworkProtocolFTP::mount(EdUrlParser *url)
{
    // Path isn't used
    return ftp->login("anonymous", "fujinet@fujinet.online", url->hostName);
}

bool NetworkProtocolFTP::umount()
{
    return ftp->logout();
}

void NetworkProtocolFTP::fserror_to_error()
{
    switch (ftp->response())
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
    case 226:
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
    }
}

bool NetworkProtocolFTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    bool res;

    res = ftp->read_file(buf,len);
    fserror_to_error();
    return res;
}

bool NetworkProtocolFTP::read_dir_entry(char *buf, unsigned short len)
{
    bool res;
    string filename;
    long filesz;

    res = ftp->read_directory(filename, filesz);
    strcpy(buf,filename.c_str());
    fserror_to_error();
    return res;
}

bool NetworkProtocolFTP::close_file_handle()
{
    ftp->close();
    return false;
}

bool NetworkProtocolFTP::close_dir_handle()
{
    ftp->close();
    return false;
}

bool NetworkProtocolFTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolFTP::status_file(NetworkStatus *status)
{
    status->rxBytesWaiting = ftp->data_available() > 65535 ? 65535 : ftp->data_available();
    status->connected = ftp->data_connected();
    status->error = ftp->data_connected() > 0 ? error : NETWORK_ERROR_END_OF_FILE;
    return false;
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

bool NetworkProtocolFTP::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolFTP::stat()
{
    return false;
}