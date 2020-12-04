/**
 * NetworkProtocolFTP
 * 
 * Implementation
 */

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
}

NetworkProtocolFTP::~NetworkProtocolFTP()
{
}

bool NetworkProtocolFTP::open_file_handle()
{
    switch(aux1_open)
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
            break;
    }

    if (ftp.open_file(path, stor))
    {
        // Error
        fserror_to_error();
        return true;
    }

    error = NETWORK_ERROR_SUCCESS;
    return false;
}

bool NetworkProtocolFTP::open_dir_handle()
{
    return true;
}

bool NetworkProtocolFTP::mount(string hostName, string path)
{
    // Path isn't used
    return ftp.login("anonymous", "fujinet@fujinet.online", hostName);
}

bool NetworkProtocolFTP::umount()
{
    return ftp.logout();
}

void NetworkProtocolFTP::fserror_to_error()
{
    int response = atoi(ftp.parse_response().c_str())
}

bool NetworkProtocolFTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolFTP::read_dir(unsigned short len)
{
    return false;
}

bool NetworkProtocolFTP::read_dir_entry(char *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolFTP::close_file_handle()
{
    return false;
}

bool NetworkProtocolFTP::close_dir_handle()
{
    return false;
}

bool NetworkProtocolFTP::write_file_handle(uint8_t *buf, unsigned short len)
{
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