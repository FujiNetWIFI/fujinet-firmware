/**
 * NetworkProtocolHTTP
 * 
 * Implementation
 */

#include "HTTP.h"
#include "status_error_codes.h"
#include "utils.h"

NetworkProtocolHTTP::NetworkProtocolHTTP(string *rx_buf, string *tx_buf, string *sp_buf)
    : NetworkProtocolFS(rx_buf, tx_buf, sp_buf)
{
    rename_implemented = true;
    delete_implemented = true;
    mkdir_implemented = true;
    rmdir_implemented = true;
}

NetworkProtocolHTTP::~NetworkProtocolHTTP()
{
}

bool NetworkProtocolHTTP::open_file_handle()
{
    return true;
}

bool NetworkProtocolHTTP::open_dir_handle()
{
    return true;
}

bool NetworkProtocolHTTP::mount(string hostName, string path)
{
    return true;
}

bool NetworkProtocolHTTP::umount()
{
    return true;
}

void NetworkProtocolHTTP::fserror_to_error()
{

}

bool NetworkProtocolHTTP::read_file_handle(uint8_t *buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolHTTP::read_dir_entry(char *buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolHTTP::close_file_handle()
{
    return true;
}

bool NetworkProtocolHTTP::close_dir_handle()
{
    return true;
}

bool NetworkProtocolHTTP::write_file_handle(uint8_t *buf, unsigned short len)
{
    return true;
}

bool NetworkProtocolHTTP::stat(string path)
{
    return true;
}

uint8_t NetworkProtocolHTTP::special_inquiry(uint8_t cmd)
{
    return 0xff;
}

bool NetworkProtocolHTTP::special_00(cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::special_40(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::special_80(uint8_t *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool NetworkProtocolHTTP::rename(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::del(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::mkdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::rmdir(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::lock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::unlock(EdUrlParser *url, cmdFrame_t *cmdFrame)
{
    return true;
}

bool NetworkProtocolHTTP::parse_dir(string s)
{
    XML_Parser parser = XML_ParserCreate(NULL);
    XML_SetUserData(parser,&dav);
    XML_SetElementHandler(parser,Start<WebDAV>,End<WebDAV>);
    XML_SetCharacterDataHandler(parser,Char<WebDAV>);
    return XML_Parse(parser,s.c_str(),s.size(),true) == XML_STATUS_ERROR;
}