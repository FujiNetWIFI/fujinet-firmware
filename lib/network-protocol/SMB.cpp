/**
 * NetworkProtocolSMB
 * 
 * Implementation
 */

#include "SMB.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolSMB::NetworkProtocolSMB(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
    Debug_printf("NetworkProtocolSMB::ctor\n");
}

NetworkProtocolSMB::~NetworkProtocolSMB()
{
    Debug_printf("NetworkProtocolSMB::dtor\n");
}

bool NetworkProtocolSMB::open_file_handle()
{
    return false;
}

bool NetworkProtocolSMB::open_dir_handle()
{
    return false;
}

bool NetworkProtocolSMB::mount(EdUrlParser *url)
{
    return false;
}

bool NetworkProtocolSMB::umount()
{
    return false;
}

void NetworkProtocolSMB::fserror_to_error()
{

}

bool NetworkProtocolSMB::read_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolSMB::read_dir_entry(char *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolSMB::close_file_handle()
{
    return false;
}

bool NetworkProtocolSMB::close_dir_handle()
{
    return false;
}

bool NetworkProtocolSMB::write_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

uint8_t NetworkProtocolSMB::special_inquiry(uint8_t cmd)
{
    return 0xff;
}

bool NetworkProtocolSMB::special_00(cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::stat()
{
    return false;
}

bool NetworkProtocolSMB::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSMB::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}