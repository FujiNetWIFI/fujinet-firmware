/**
 * NetworkProtocolSD
 * 
 * Implementation
 */

#include "../../include/debug.h"

#include "SD.h"
#include "status_error_codes.h"

NetworkProtocolSD::NetworkProtocolSD(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{

}

NetworkProtocolSD::~NetworkProtocolSD()
{

}

bool NetworkProtocolSD::open_file_handle()
{
    return false;
}

bool NetworkProtocolSD::open_dir_handle()
{
    return false;
}

bool NetworkProtocolSD::mount(EdUrlParser *url)
{
    return false;
}

bool NetworkProtocolSD::umount()
{
    return false;
}

void NetworkProtocolSD::fserror_to_error()
{
    
}

bool NetworkProtocolSD::read_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolSD::read_dir_entry(char *buf, unsigned short len)
{
    return false;
}

bool NetworkProtocolSD::close_file_handle()
{
    return false;
}

bool NetworkProtocolSD::close_dir_handle()
{
    return false;
}

bool NetworkProtocolSD::write_file_handle(uint8_t *buf, unsigned short len)
{
    return false;
}

uint8_t NetworkProtocolSD::special_inquiry(uint8_t cmd)
{
    return 0;
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

bool NetworkProtocolSD::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::stat()
{
    return false;
}

bool NetworkProtocolSD::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolSD::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return false;
}
