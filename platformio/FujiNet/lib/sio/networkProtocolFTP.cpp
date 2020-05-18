#include "networkProtocolFTP.h"
#include "../../include/debug.h"

bool networkProtocolFTP::ftpExpect(string resultCode)
{
    char buf[512];
    string sbuf;

    if (!control.connected())
        return false;

    control.readBytesUntil('\n', buf, sizeof(buf));
    sbuf = string(buf);
    controlResponse = sbuf.substr(4);

    return (resultCode.find_first_of(resultCode) == 0 ? true : false);
}

unsigned short networkProtocolFTP::parsePort(string response)
{
    size_t pos_start = 0;
    size_t pos_end = 0;

    pos_start = response.find_first_of("|");

    for (int i = 0; i < 2; i++)
    {
        pos_start = response.find("|");
    }

    pos_end = response.find_last_of("|");
    pos_start++;
    pos_end--;

    return (atoi(response.substr(pos_start, pos_end).c_str()));
}

networkProtocolFTP::networkProtocolFTP()
{
}

networkProtocolFTP::~networkProtocolFTP()
{
}

bool networkProtocolFTP::open(EdUrlParser *urlParser, cmdFrame_t *cmdFrame)
{
    if (urlParser->port.empty())
        urlParser->port = "21";

    hostName = urlParser->hostName;

    if (!control.connect(urlParser->hostName.c_str(), atoi(urlParser->port.c_str())))
        return true; // Error

    if (!ftpExpect("220"))
        return true; // error

    control.write("USER anonymous\r\n");

    if (!ftpExpect("331"))
        return true;

    control.write("PASS fujinet@fujinet.online\r\n");

    if (!ftpExpect("230"))
        return true;

    control.write("SIZE ");
    control.write(urlParser->path.c_str());
    control.write("\r\n");

    if (!ftpExpect("213"))
        return true;

    dataSize = atol(controlResponse.c_str());

    control.write("EPSV");

    if (!ftpExpect("229"))
        return true;

    dataPort = parsePort(controlResponse);

    control.write("RETR ");
    control.write(urlParser->path.c_str());
    control.write("\r\n");

    return false;
}

bool networkProtocolFTP::close()
{
    if (data.connected())
        data.stop();
    
    if (control.connected())
    {
        control.write("QUIT\r\n");
    }

    ftpExpect("221");

    control.stop();
    return false;
}

bool networkProtocolFTP::read(byte *rx_buf, unsigned short len)
{
    if (!data.connected())
    {
        if (!data.connect(hostName.c_str(),dataPort))
            return true;
    }
    data.readBytes(rx_buf,len);
    return false;
}

bool networkProtocolFTP::write(byte *tx_buf, unsigned short len)
{
    return false;
}

bool networkProtocolFTP::status(byte *status_buf)
{
    unsigned short available_bytes = (dataSize > 65535 ? 65535 : (unsigned short)dataSize);
    status_buf[0] = available_bytes & 0xFF;
    status_buf[1] = available_bytes >> 8;
    status_buf[2] = 1;
    status_buf[3] = 0;
    return false;
}

bool networkProtocolFTP::special(byte *sp_buf, unsigned short len, cmdFrame_t *cmdFrame)
{
    return false;
}

bool networkProtocolFTP::special_supported_00_command(unsigned char comnd)
{
    return false;
}